/***********************************/
/*      SPICE Modeling for VPR     */
/*       Xifan TANG, EPFL/LSI      */
/***********************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

/* Include vpr structs*/
#include "vtr_assert.h"
#include "util.h"
#include "physical_types.h"
#include "vpr_types.h"
#include "globals.h"
#include "rr_graph.h"
#include "vpr_utils.h"
#include "path_delay.h"
#include "stats.h"

/* Include spice support headers*/
#include "linkedlist.h"
#include "circuit_library_utils.h"
#include "fpga_x2p_utils.h"
#include "fpga_x2p_backannotate_utils.h"
#include "fpga_x2p_setup.h"
#include "fpga_x2p_naming.h"

#include "mux_library_builder.h"
#include "build_device_module.h"
#include "build_device_bitstream.h"
#include "build_fabric_bitstream.h"
#include "bitstream_writer.h"

#include "spice_api.h"
#include "verilog_api.h"
#include "sdc_api.h"
#include "fpga_bitstream.h"

#include "fpga_x2p_reserved_words.h"
#include "fpga_x2p_globals.h"
#include "fpga_x2p_api.h"

/* Top-level API of FPGA-SPICE */
void vpr_fpga_x2p_tool_suites(t_vpr_setup vpr_setup,
                              t_arch Arch) {
  t_sram_orgz_info* sram_bitstream_orgz_info = NULL;

  /* Common initializations and malloc operations */
  /* If FPGA-SPICE is not called, we should initialize the spice_models */
  if (TRUE == vpr_setup.FPGA_SPICE_Opts.do_fpga_spice) {
    fpga_x2p_setup(vpr_setup, &Arch);
  }

  /* Build multiplexer graphs */
  MuxLibrary mux_lib = build_device_mux_library(num_rr_nodes, rr_node, switch_inf, Arch.spice->circuit_lib, &vpr_setup.RoutingArch);

  /* TODO: Build global routing architecture modules */
  /* Create a vector of switch infs. TODO: this should be replaced switch objects!!! */
  std::vector<t_switch_inf> rr_switches;
  for (short i = 0; i < vpr_setup.RoutingArch.num_switch; ++i) {
    rr_switches.push_back(switch_inf[i]);
  }

  /* TODO: This should be done outside this function!!! */
  vtr::Point<size_t> device_size(nx + 2, ny + 2);
  std::vector<std::vector<t_grid_tile>> grids;
  /* Organize a vector (matrix) of grids to feed the top-level module generation */
  grids.resize(device_size.x());
  for (size_t ix = 0; ix < device_size.x(); ++ix) {
    grids[ix].resize(device_size.y());
    for (size_t iy = 0; iy < device_size.y(); ++iy) {
      grids[ix][iy] = grid[ix][iy];
    }
  } 

  /* Organize a vector (matrix) of clb2clb directs to feed the top-level module generation */
  std::vector<t_clb_to_clb_directs> clb2clb_directs;
  for (int i = 0; i < num_clb2clb_directs; ++i) {
    clb2clb_directs.push_back(clb2clb_direct[i]); 
  }

  /* Organize a vector for logical blocks to feed Verilog generator */
  std::vector<t_logical_block> L_logical_blocks;
  for (int i = 0; i < num_logical_blocks; ++i) {
    L_logical_blocks.push_back(logical_block[i]);
  }

  /* Organize a vector for blocks to feed Verilog generator */
  std::vector<t_block> L_blocks;
  for (int i = 0; i < num_blocks; ++i) {
    L_blocks.push_back(block[i]);
  }

  /* Build module graphs */
  ModuleManager module_manager = build_device_module_graph(vpr_setup, Arch, mux_lib, 
                                                           device_size, grids, 
                                                           rr_switches, clb2clb_directs, device_rr_gsb);

  /* Build bitstream database if needed */
  BitstreamManager bitstream_manager;
  std::vector<ConfigBitId> fabric_bitstream;
  if ( (TRUE == vpr_setup.FPGA_SPICE_Opts.BitstreamGenOpts.gen_bitstream)
    || (TRUE == vpr_setup.FPGA_SPICE_Opts.SpiceOpts.do_spice)
    || (TRUE == vpr_setup.FPGA_SPICE_Opts.SynVerilogOpts.dump_syn_verilog)) {

    /* Build fabric independent bitstream */
    bitstream_manager = build_device_bitstream(vpr_setup, Arch, module_manager, 
                                               Arch.spice->circuit_lib, mux_lib, 
                                               device_size, grids, 
                                               rr_switches, rr_node, device_rr_gsb);

    /* Build fabric dependent bitstream */
    fabric_bitstream = build_fabric_dependent_bitstream(bitstream_manager, module_manager);

    /* Write bitstream to files */
    std::string bitstream_file_path; 

    if (NULL == vpr_setup.FPGA_SPICE_Opts.BitstreamGenOpts.bitstream_output_file) {
      bitstream_file_path = std::string(vpr_setup.FileNameOpts.CircuitName);
      bitstream_file_path.append(BITSTREAM_XML_FILE_NAME_POSTFIX);
    } else {
      bitstream_file_path = vpr_setup.FPGA_SPICE_Opts.BitstreamGenOpts.bitstream_output_file;
    }

    write_arch_independent_bitstream_to_xml_file(bitstream_manager, bitstream_file_path);
  }

  /* Xifan TANG: SPICE Modeling, SPICE Netlist Output  */ 
  if (TRUE == vpr_setup.FPGA_SPICE_Opts.SpiceOpts.do_spice) {
    vpr_fpga_spice(vpr_setup, Arch, vpr_setup.FileNameOpts.CircuitName);
  }

  /* Xifan TANG: Synthesizable verilog dumping */
  if (TRUE == vpr_setup.FPGA_SPICE_Opts.SynVerilogOpts.dump_syn_verilog) {
    /* Create a local SRAM organization info
     * TODO: This should be deprecated in future */
    VTR_ASSERT(NULL != Arch.sram_inf.verilog_sram_inf_orgz); /* Check !*/
    t_spice_model* sram_verilog_model = Arch.sram_inf.verilog_sram_inf_orgz->spice_model;
    /* initialize the SRAM organization information struct */
    t_sram_orgz_info* sram_verilog_orgz_info = alloc_one_sram_orgz_info();
    init_sram_orgz_info(sram_verilog_orgz_info, Arch.sram_inf.verilog_sram_inf_orgz->type, sram_verilog_model, nx + 2, ny + 2);

    vpr_fpga_verilog(module_manager, bitstream_manager, fabric_bitstream, mux_lib, 
                     L_logical_blocks, device_size, grids, L_blocks, device_rr_gsb,
                     vpr_setup, Arch, std::string(vpr_setup.FileNameOpts.CircuitName), sram_verilog_orgz_info);
  }	


  /* Run SDC Generator */
  std::string src_dir = find_path_dir_name(std::string(vpr_setup.FileNameOpts.CircuitName));

  /* Use current directory if there is not dir path given */
  if (false == src_dir.empty()) {
    src_dir = format_dir_path(src_dir);
  }
  SdcOption sdc_options(format_dir_path(src_dir + std::string(FPGA_X2P_DEFAULT_SDC_DIR)));
  sdc_options.set_generate_sdc_pnr(TRUE == vpr_setup.FPGA_SPICE_Opts.SynVerilogOpts.print_sdc_pnr);
  sdc_options.set_generate_sdc_analysis(TRUE == vpr_setup.FPGA_SPICE_Opts.SynVerilogOpts.print_sdc_analysis);

  /* Create directory to contain the SDC files */
  create_dir_path(sdc_options.sdc_dir().c_str());

  if (true == sdc_options.generate_sdc()) {
    std::vector<CircuitPortId> global_ports = find_circuit_library_global_ports(Arch.spice->circuit_lib);
    /* TODO: the critical path delay unit should be explicit! */
    fpga_sdc_generator(sdc_options, 
                       Arch.spice->spice_params.stimulate_params.vpr_crit_path_delay / 1e-9,
                       rr_switches, device_rr_gsb, 
                       L_logical_blocks, device_size, grids, L_blocks, 
                       module_manager, mux_lib, 
                       Arch.spice->circuit_lib, global_ports,
                       TRUE == vpr_setup.FPGA_SPICE_Opts.compact_routing_hierarchy);
  }

  /* Xifan Tang: Bitstream Generator */
  if ((TRUE == vpr_setup.FPGA_SPICE_Opts.BitstreamGenOpts.gen_bitstream)
    &&(FALSE == vpr_setup.FPGA_SPICE_Opts.SpiceOpts.do_spice)
    &&(FALSE == vpr_setup.FPGA_SPICE_Opts.SynVerilogOpts.dump_syn_verilog)) {
    /* Run bitstream generation here only when other functionalities are disabled;
     * bitstream will be run inside SPICE and Verilog Generators
     */
    vpr_fpga_bitstream_generator(vpr_setup, Arch, vpr_setup.FileNameOpts.CircuitName, &sram_bitstream_orgz_info);
    /* Free sram_orgz_info */
    free_sram_orgz_info(sram_bitstream_orgz_info,
                        sram_bitstream_orgz_info->type);
  }

  /* Free */
  if (TRUE == vpr_setup.FPGA_SPICE_Opts.do_fpga_spice) {
    /* Free all the backannotation containing post routing information */
    free_backannotate_vpr_post_route_info();
    /* TODO: free other linked lists ! */
    fpga_x2p_free(&Arch);
  }


  return;
}

