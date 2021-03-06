/********************************************************************
 * This file includes functions that print SDC (Synopsys Design Constraint) 
 * files in physical design tools, i.e., Place & Route (PnR) tools
 * The SDC files are used to constrain the physical design for each routing modules
 * in FPGA fabric, such as Switch Blocks (SBs) and Connection Blocks (CBs)
 *
 * Note that this is different from the SDC to constrain VPR Place&Route
 * engine! These SDCs are designed for PnR to generate FPGA layouts!!!
 *******************************************************************/
#include <ctime>
#include <fstream>

#include "vtr_assert.h"
#include "device_port.h"

#include "util.h"
#include "mux_utils.h"

#include "fpga_x2p_naming.h"
#include "fpga_x2p_utils.h"

#include "build_routing_module_utils.h"

#include "sdc_writer_naming.h"
#include "sdc_writer_utils.h"
#include "pnr_sdc_routing_writer.h"

/********************************************************************
 * Find the timing constraints between the inputs and outputs of a routing
 * multiplexer in a Switch Block
 *******************************************************************/
static 
float find_pnr_sdc_switch_tmax(const t_switch_inf& switch_inf) {
  return switch_inf.R * switch_inf.Cout + switch_inf.Tdel;
}

/********************************************************************
 * Set timing constraints between the inputs and outputs of a routing
 * multiplexer in a Switch Block
 *******************************************************************/
static 
void print_pnr_sdc_constrain_sb_mux_timing(std::fstream& fp,
                                           const ModuleManager& module_manager,
                                           const ModuleId& sb_module, 
                                           const RRGSB& rr_gsb,
                                           const std::vector<t_switch_inf>& switches,
                                           const e_side& output_node_side,
                                           t_rr_node* output_rr_node) {
  /* Validate file stream */
  check_file_handler(fp);

  VTR_ASSERT(  ( CHANX == output_rr_node->type )
            || ( CHANY == output_rr_node->type ));

  /* Find the module port corresponding to the output rr_node */
  ModulePortId module_output_port = find_switch_block_module_chan_port(module_manager, 
                                                                       sb_module,
                                                                       rr_gsb, 
                                                                       output_node_side,
                                                                       output_rr_node,
                                                                       OUT_PORT);

  /* Find the module port corresponding to the fan-in rr_nodes of the output rr_node */
  std::vector<t_rr_node*> input_rr_nodes;
  for (int iedge = 0; iedge < output_rr_node->num_drive_rr_nodes; iedge++) {
    input_rr_nodes.push_back(output_rr_node->drive_rr_nodes[iedge]);
  }
     
  std::vector<ModulePortId> module_input_ports = find_switch_block_module_input_ports(module_manager,
                                                                                      sb_module, 
                                                                                      rr_gsb, 
                                                                                      input_rr_nodes);

  /* Find timing constraints for each path (edge) */
  std::map<ModulePortId, float> switch_delays;
  for (int iedge = 0; iedge < output_rr_node->num_drive_rr_nodes; iedge++) {
    /* Get the switch delay */
    int switch_id = output_rr_node->drive_switches[iedge];
    switch_delays[module_input_ports[iedge]] = find_pnr_sdc_switch_tmax(switches[switch_id]);
  }

  /* Find the starting points */
  for (const ModulePortId& module_input_port : module_input_ports) {
    /* Constrain a path */
    print_pnr_sdc_constrain_port2port_timing(fp,
                                             module_manager, 
                                             sb_module, module_input_port, 
                                             sb_module, module_output_port,
                                             switch_delays[module_input_port]);
  }
}

/********************************************************************
 * Set timing constraints between the inputs and outputs of SBs, 
 * which are connected by routing multiplexers with the given delays
 * specified in architectural XML file
 *
 * To enable block by block timing constraining, we generate the SDC
 * file for each unique SB module
 *******************************************************************/
static 
void print_pnr_sdc_constrain_sb_timing(const std::string& sdc_dir,
                                       const ModuleManager& module_manager,
                                       const std::vector<t_switch_inf>& switches,
                                       const RRGSB& rr_gsb) {

  /* Create the file name for Verilog netlist */
  vtr::Point<size_t> gsb_coordinate(rr_gsb.get_sb_x(), rr_gsb.get_sb_y());
  std::string sdc_fname(sdc_dir + generate_switch_block_module_name(gsb_coordinate) + std::string(SDC_FILE_NAME_POSTFIX));

  /* Create the file stream */
  std::fstream fp;
  fp.open(sdc_fname, std::fstream::out | std::fstream::trunc);

  /* Validate file stream */
  check_file_handler(fp);

  std::string sb_module_name = generate_switch_block_module_name(gsb_coordinate);
  ModuleId sb_module = module_manager.find_module(sb_module_name);
  VTR_ASSERT(true == module_manager.valid_module_id(sb_module));

  /* Generate the descriptions*/
  print_sdc_file_header(fp, std::string("Constrain timing of Switch Block " + sb_module_name + " for PnR"));

  for (size_t side = 0; side < rr_gsb.get_num_sides(); ++side) {
    Side side_manager(side);
    for (size_t itrack = 0; itrack < rr_gsb.get_chan_width(side_manager.get_side()); ++itrack) {
      t_rr_node* chan_rr_node = rr_gsb.get_chan_node(side_manager.get_side(), itrack);
      /* We only care the output port and it should indicate a SB mux */
      if (OUT_PORT != rr_gsb.get_chan_node_direction(side_manager.get_side(), itrack)) { 
        continue; 
      }
      /* Constrain thru wires */
      if (false != rr_gsb.is_sb_node_passing_wire(side_manager.get_side(), itrack)) {
        continue;
      } 
      /* This is a MUX, constrain all the paths from an input to an output */
      print_pnr_sdc_constrain_sb_mux_timing(fp,
                                            module_manager, sb_module, 
                                            rr_gsb,
                                            switches,
                                            side_manager.get_side(),
                                            chan_rr_node);
    }
  }

  /* Close file handler */
  fp.close();
}

/********************************************************************
 * Print SDC timing constraints for Switch blocks
 * This function is designed for flatten routing hierarchy
 *******************************************************************/
void print_pnr_sdc_flatten_routing_constrain_sb_timing(const std::string& sdc_dir,
                                                       const ModuleManager& module_manager,
                                                       const std::vector<t_switch_inf>& switches,
                                                       const DeviceRRGSB& L_device_rr_gsb) {
  vpr_printf(TIO_MESSAGE_INFO, 
             "Generating SDC for constrain Switch Block timing for P&R flow...");

  /* Start time count */
  clock_t t_start = clock();

  /* Get the range of SB array */
  DeviceCoordinator sb_range = L_device_rr_gsb.get_gsb_range();
  /* Go for each SB */
  for (size_t ix = 0; ix < sb_range.get_x(); ++ix) {
    for (size_t iy = 0; iy < sb_range.get_y(); ++iy) {
      const RRGSB& rr_gsb = L_device_rr_gsb.get_gsb(ix, iy);
      print_pnr_sdc_constrain_sb_timing(sdc_dir,
                                        module_manager,
                                        switches,
                                        rr_gsb);
    }
  }
  
  /* End time count */
  clock_t t_end = clock();

  float run_time_sec = (float)(t_end - t_start) / CLOCKS_PER_SEC;
  vpr_printf(TIO_MESSAGE_INFO, 
             "took %g seconds\n", 
             run_time_sec);  
}

/********************************************************************
 * Print SDC timing constraints for Switch blocks
 * This function is designed for compact routing hierarchy
 *******************************************************************/
void print_pnr_sdc_compact_routing_constrain_sb_timing(const std::string& sdc_dir,
                                                       const ModuleManager& module_manager,
                                                       const std::vector<t_switch_inf>& switches,
                                                       const DeviceRRGSB& L_device_rr_gsb) {
  vpr_printf(TIO_MESSAGE_INFO, 
             "Generating SDC for constrain Switch Block timing for P&R flow...");

  /* Start time count */
  clock_t t_start = clock();

  for (size_t isb = 0; isb < L_device_rr_gsb.get_num_sb_unique_module(); ++isb) {
    const RRGSB& rr_gsb = L_device_rr_gsb.get_sb_unique_module(isb);
    print_pnr_sdc_constrain_sb_timing(sdc_dir,
                                      module_manager,
                                      switches,
                                      rr_gsb);
  }
  
  /* End time count */
  clock_t t_end = clock();

  float run_time_sec = (float)(t_end - t_start) / CLOCKS_PER_SEC;
  vpr_printf(TIO_MESSAGE_INFO, 
             "took %g seconds\n", 
             run_time_sec);  
}

/********************************************************************
 * Set timing constraints between the inputs and outputs of a routing
 * multiplexer in a Connection Block
 *******************************************************************/
static 
void print_pnr_sdc_constrain_cb_mux_timing(std::fstream& fp,
                                           const ModuleManager& module_manager,
                                           const ModuleId& cb_module, 
                                           const RRGSB& rr_gsb,
                                           const t_rr_type& cb_type,
                                           const std::vector<t_switch_inf>& switches,
                                           t_rr_node* output_rr_node) {
  /* Validate file stream */
  check_file_handler(fp);

  VTR_ASSERT(IPIN == output_rr_node->type);
  
  /* We have OPINs since we may have direct connections:
   * These connections should be handled by other functions in the compact_netlist.c 
   * So we just return here for OPINs 
   */
  if ( (1 == output_rr_node->num_drive_rr_nodes)
    && (OPIN == output_rr_node->drive_rr_nodes[0]->type) ) {
    return;
  }

  /* Find the module port corresponding to the output rr_node */
  ModulePortId module_output_port = find_connection_block_module_ipin_port(module_manager, 
                                                                           cb_module,
                                                                           rr_gsb, 
                                                                           output_rr_node);

  /* Find the module port corresponding to the fan-in rr_nodes of the output rr_node */
  std::vector<t_rr_node*> input_rr_nodes;
  for (int iedge = 0; iedge < output_rr_node->num_drive_rr_nodes; iedge++) {
    /* Skip OPINs which should be handled in direct connection */
    input_rr_nodes.push_back(output_rr_node->drive_rr_nodes[iedge]);
  }
     
  std::vector<ModulePortId> module_input_ports = find_connection_block_module_input_ports(module_manager,
                                                                                          cb_module, 
                                                                                          rr_gsb, 
                                                                                          cb_type,
                                                                                          input_rr_nodes);

  /* Find timing constraints for each path (edge) */
  std::map<ModulePortId, float> switch_delays;
  for (int iedge = 0; iedge < output_rr_node->num_drive_rr_nodes; iedge++) {
    /* Get the switch delay */
    int switch_id = output_rr_node->drive_switches[iedge];
    switch_delays[module_input_ports[iedge]] = find_pnr_sdc_switch_tmax(switches[switch_id]);
  }

  /* Find the starting points */
  for (const ModulePortId& module_input_port : module_input_ports) {
    /* Constrain a path */
    print_pnr_sdc_constrain_port2port_timing(fp,
                                             module_manager, 
                                             cb_module, module_input_port, 
                                             cb_module, module_output_port,
                                             switch_delays[module_input_port]);
  }
}


/********************************************************************
 * Print SDC timing constraints for a Connection block 
 * This function is designed for compact routing hierarchy
 *******************************************************************/
static 
void print_pnr_sdc_constrain_cb_timing(const std::string& sdc_dir,
                                       const ModuleManager& module_manager,
                                       const RRGSB& rr_gsb, 
                                       const t_rr_type& cb_type,
                                       const std::vector<t_switch_inf>& switches) {
  /* Create the netlist */
  vtr::Point<size_t> gsb_coordinate(rr_gsb.get_cb_x(cb_type), rr_gsb.get_cb_y(cb_type));

  /* Find the module name and create a SDC file for it */
  std::string sdc_fname(sdc_dir + generate_connection_block_module_name(cb_type, gsb_coordinate) + std::string(SDC_FILE_NAME_POSTFIX));

  /* Create the file stream */
  std::fstream fp;
  fp.open(sdc_fname, std::fstream::out | std::fstream::trunc);

  /* Validate file stream */
  check_file_handler(fp);

  std::string cb_module_name = generate_connection_block_module_name(cb_type, gsb_coordinate); 
  ModuleId cb_module = module_manager.find_module(cb_module_name);
  VTR_ASSERT(true == module_manager.valid_module_id(cb_module));

  /* Generate the descriptions*/
  print_sdc_file_header(fp, std::string("Constrain timing of Connection Block " + cb_module_name + " for PnR"));

  std::vector<enum e_side> cb_sides = rr_gsb.get_cb_ipin_sides(cb_type);

  for (size_t side = 0; side < cb_sides.size(); ++side) {
    enum e_side cb_ipin_side = cb_sides[side];
    Side side_manager(cb_ipin_side);
    for (size_t inode = 0; inode < rr_gsb.get_num_ipin_nodes(cb_ipin_side); ++inode) {
      t_rr_node* ipin_rr_node = rr_gsb.get_ipin_node(cb_ipin_side, inode);
      print_pnr_sdc_constrain_cb_mux_timing(fp,
                                            module_manager, cb_module, 
                                            rr_gsb, cb_type,
                                            switches,
                                            ipin_rr_node);
    }
  }

  /* Close file handler */
  fp.close();
}

/********************************************************************
 * Iterate over all the connection blocks in a device
 * and print SDC file for each of them 
 *******************************************************************/
static 
void print_pnr_sdc_flatten_routing_constrain_cb_timing(const std::string& sdc_dir,
                                                       const ModuleManager& module_manager, 
                                                       const DeviceRRGSB& L_device_rr_gsb,
                                                       const std::vector<t_switch_inf>& switches,
                                                       const t_rr_type& cb_type) {
  /* Build unique X-direction connection block modules */
  DeviceCoordinator cb_range = L_device_rr_gsb.get_gsb_range();

  for (size_t ix = 0; ix < cb_range.get_x(); ++ix) {
    for (size_t iy = 0; iy < cb_range.get_y(); ++iy) {
      /* Check if the connection block exists in the device!
       * Some of them do NOT exist due to heterogeneous blocks (height > 1) 
       * We will skip those modules
       */
      const RRGSB& rr_gsb = L_device_rr_gsb.get_gsb(ix, iy);
      if (false == rr_gsb.is_cb_exist(cb_type)) {
        continue;
      }
      print_pnr_sdc_constrain_cb_timing(sdc_dir,
                                        module_manager,
                                        rr_gsb, 
                                        cb_type,
                                        switches);

    }
  }
}

/********************************************************************
 * Iterate over all the connection blocks in a device
 * and print SDC file for each of them 
 *******************************************************************/
void print_pnr_sdc_flatten_routing_constrain_cb_timing(const std::string& sdc_dir,
                                                       const ModuleManager& module_manager, 
                                                       const DeviceRRGSB& L_device_rr_gsb,
                                                       const std::vector<t_switch_inf>& switches) {
  vpr_printf(TIO_MESSAGE_INFO, 
             "Generating SDC for constrain Connection Block timing for P&R flow...");

  /* Start time count */
  clock_t t_start = clock();

  print_pnr_sdc_flatten_routing_constrain_cb_timing(sdc_dir, module_manager, 
                                                    L_device_rr_gsb,
                                                    switches,
                                                    CHANX);

  print_pnr_sdc_flatten_routing_constrain_cb_timing(sdc_dir, module_manager, 
                                                    L_device_rr_gsb,
                                                    switches,
                                                    CHANY);

  /* End time count */
  clock_t t_end = clock();

  float run_time_sec = (float)(t_end - t_start) / CLOCKS_PER_SEC;
  vpr_printf(TIO_MESSAGE_INFO, 
             "took %g seconds\n", 
             run_time_sec);  
}

/********************************************************************
 * Print SDC timing constraints for Connection blocks
 * This function is designed for compact routing hierarchy
 *******************************************************************/
void print_pnr_sdc_compact_routing_constrain_cb_timing(const std::string& sdc_dir,
                                                       const ModuleManager& module_manager,
                                                       const std::vector<t_switch_inf>& switches,
                                                       const DeviceRRGSB& L_device_rr_gsb) {
  vpr_printf(TIO_MESSAGE_INFO, 
             "Generating SDC for constrain Connection Block timing for P&R flow...");

  /* Start time count */
  clock_t t_start = clock();

  /* Print SDC for unique X-direction connection block modules */
  for (size_t icb = 0; icb < L_device_rr_gsb.get_num_cb_unique_module(CHANX); ++icb) {
    const RRGSB& unique_mirror = L_device_rr_gsb.get_cb_unique_module(CHANX, icb);
    print_pnr_sdc_constrain_cb_timing(sdc_dir,
                                      module_manager,
                                      unique_mirror, 
                                      CHANX,
                                      switches);
  }

  /* Print SDC for unique Y-direction connection block modules */
  for (size_t icb = 0; icb < L_device_rr_gsb.get_num_cb_unique_module(CHANY); ++icb) {
    const RRGSB& unique_mirror = L_device_rr_gsb.get_cb_unique_module(CHANY, icb);
    print_pnr_sdc_constrain_cb_timing(sdc_dir,
                                      module_manager,
                                      unique_mirror, 
                                      CHANY,
                                      switches);
  }

  /* End time count */
  clock_t t_end = clock();

  float run_time_sec = (float)(t_end - t_start) / CLOCKS_PER_SEC;
  vpr_printf(TIO_MESSAGE_INFO, 
             "took %g seconds\n", 
             run_time_sec);  
}

