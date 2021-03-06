/********************************************************************
 * This file includes most utilized function for rr_block data structures
 *******************************************************************/
#include <vector>
#include <algorithm>

#include "vtr_assert.h"
#include "vpr_types.h"
#include "mux_utils.h"
#include "fpga_x2p_types.h"
#include "rr_blocks_utils.h"

/*********************************************************************
 * This function will find the global ports required by a Connection Block
 * module. It will find all the circuit models in the circuit library 
 * that may be included in the connection block
 * Collect the global ports from the circuit_models and merge with the same name
 ********************************************************************/
std::vector<CircuitPortId> find_connection_block_global_ports(const RRGSB& rr_gsb, 
                                                              const t_rr_type& cb_type,
                                                              const CircuitLibrary& circuit_lib,
                                                              const std::vector<t_switch_inf>& switch_lib) {
  std::vector<CircuitModelId> sub_models;
  /* Walk through the OUTPUT nodes at each side of a GSB, 
   * get the switch id of incoming edges 
   * and get the circuit model linked to the switch id
   */
  std::vector<enum e_side> cb_ipin_sides = rr_gsb.get_cb_ipin_sides(cb_type);
  for (size_t iside = 0; iside < cb_ipin_sides.size(); ++iside) {
    enum e_side cb_ipin_side = cb_ipin_sides[iside];
    for (size_t inode = 0; inode < rr_gsb.get_num_ipin_nodes(cb_ipin_side); ++inode) {
      /* Find the size of routing multiplexers driving this IPIN node */
      int mux_size = rr_gsb.get_ipin_node(cb_ipin_side, inode)->fan_in;
      /* Bypass fan_in == 1 or 0, they are not considered as routing multiplexers */
      if (2 > mux_size) {
        continue;
      }
      /* Find the driver switch of the node */
      short driver_switch = rr_gsb.get_ipin_node(cb_ipin_side, inode)->drive_switches[DEFAULT_SWITCH_ID];
      /* Find the circuit model id of the driver switch */
      VTR_ASSERT( (size_t)driver_switch < switch_lib.size() );
      /* Get the model, and try to add to the sub_model list */
      CircuitModelId switch_circuit_model = switch_lib[driver_switch].circuit_model;
      /* Make sure it is a valid id */
      VTR_ASSERT( CircuitModelId::INVALID() != switch_circuit_model );
      /* Get the model, and try to add to the sub_model list */
      if (sub_models.end() == std::find(sub_models.begin(), sub_models.end(), switch_circuit_model)) {
        /* Not yet in the list, add it */
        sub_models.push_back(switch_circuit_model);
      }
    }
  }

  std::vector<CircuitPortId> global_ports;
  /* Iterate over the model list, and add the global ports*/
  for (const auto& model : sub_models) {
    std::vector<CircuitPortId> temp_global_ports = circuit_lib.model_global_ports(model, true);
    /* Add the temp_global_ports to the list to be returned, make sure we do not have any duplicated ports */
    for (const auto& port_candidate : temp_global_ports) {
      if (global_ports.end() == std::find(global_ports.begin(), global_ports.end(), port_candidate)) {
        /* Not yet in the list, add it */
        global_ports.push_back(port_candidate);
      }
    }
  }

  return global_ports;
}

 
/*********************************************************************
 * This function will find the global ports required by a Switch Block
 * module. It will find all the circuit models in the circuit library 
 * that may be included in the Switch Block
 * Collect the global ports from the circuit_models and merge with the same name
 ********************************************************************/
std::vector<CircuitPortId> find_switch_block_global_ports(const RRGSB& rr_gsb, 
                                                          const CircuitLibrary& circuit_lib,
                                                          const std::vector<t_switch_inf>& switch_lib) {
  std::vector<CircuitModelId> sub_models;
  /* Walk through the OUTPUT nodes at each side of a GSB, 
   * get the switch id of incoming edges 
   * and get the circuit model linked to the switch id
   */
  for (size_t side = 0; side < rr_gsb.get_num_sides(); ++side) {
    Side side_manager(side);
    for (size_t itrack = 0; itrack < rr_gsb.get_chan_width(side_manager.get_side()); ++itrack) {
      if (OUT_PORT != rr_gsb.get_chan_node_direction(side_manager.get_side(), itrack)) {
        continue;
      }
      /* Find the driver switch of the node */
      short driver_switch = rr_gsb.get_chan_node(side_manager.get_side(), itrack)->drive_switches[DEFAULT_SWITCH_ID];
      /* Find the circuit model id of the driver switch */
      VTR_ASSERT( (size_t)driver_switch < switch_lib.size() );
      /* Get the model, and try to add to the sub_model list */
      CircuitModelId switch_circuit_model = switch_lib[driver_switch].circuit_model;
      /* Make sure it is a valid id */
      VTR_ASSERT( CircuitModelId::INVALID() != switch_circuit_model );
      /* Get the model, and try to add to the sub_model list */
      if (sub_models.end() == std::find(sub_models.begin(), sub_models.end(), switch_circuit_model)) {
        /* Not yet in the list, add it */
        sub_models.push_back(switch_circuit_model);
      }
    }
  }

  std::vector<CircuitPortId> global_ports;
  /* Iterate over the model list, and add the global ports*/
  for (const auto& model : sub_models) {
    std::vector<CircuitPortId> temp_global_ports = circuit_lib.model_global_ports(model, true);
    /* Add the temp_global_ports to the list to be returned, make sure we do not have any duplicated ports */
    for (const auto& port_candidate : temp_global_ports) {
      if (global_ports.end() == std::find(global_ports.begin(), global_ports.end(), port_candidate)) {
        /* Not yet in the list, add it */
        global_ports.push_back(port_candidate);
      }
    }
  }

  return global_ports;
}

/*********************************************************************
 * This function will find the number of multiplexers required by 
 * a connection Block module. 
 ********************************************************************/
size_t find_connection_block_number_of_muxes(const RRGSB& rr_gsb,
                                             const t_rr_type& cb_type) {
  size_t num_muxes = 0;

  std::vector<enum e_side> cb_ipin_sides = rr_gsb.get_cb_ipin_sides(cb_type);
  for (size_t iside = 0; iside < cb_ipin_sides.size(); ++iside) {
    enum e_side cb_ipin_side = cb_ipin_sides[iside];
    for (size_t inode = 0; inode < rr_gsb.get_num_ipin_nodes(cb_ipin_side); ++inode) {
      /* Find the size of routing multiplexers driving this IPIN node */
      int mux_size = rr_gsb.get_ipin_node(cb_ipin_side, inode)->fan_in;
      /* Bypass fan_in == 1 or 0, they are not considered as routing multiplexers */
      if (2 > mux_size) {
        continue;
      }
      /* This means we need a multiplexer, update the counter */
      num_muxes++;
    }
  }

  return num_muxes;
}

/*********************************************************************
 * This function will find the number of multiplexers required by 
 * a Switch Block module. 
 ********************************************************************/
size_t find_switch_block_number_of_muxes(const RRGSB& rr_gsb) {
  size_t num_muxes = 0;
  /* Walk through the OUTPUT nodes at each side of a GSB, 
   * get the switch id of incoming edges 
   * and get the circuit model linked to the switch id
   */
  for (size_t side = 0; side < rr_gsb.get_num_sides(); ++side) {
    Side side_manager(side);
    for (size_t itrack = 0; itrack < rr_gsb.get_chan_width(side_manager.get_side()); ++itrack) {
      if (OUT_PORT != rr_gsb.get_chan_node_direction(side_manager.get_side(), itrack)) {
        continue;
      }
      /* Check if this node is just a passing wire */
      if (true == rr_gsb.is_sb_node_passing_wire(side_manager.get_side(), itrack)) {
        continue;
      }
      /* Check if this node has more than 2 drivers */
	  if (2 > rr_gsb.get_chan_node(side_manager.get_side(), itrack)->num_drive_rr_nodes) {
        continue;
      }
      /* This means we need a multiplexer, update the counter */
      num_muxes++;
    }
  }
  return num_muxes;
}

/*********************************************************************
 * Find the number of configuration bits of a Connection Block
 ********************************************************************/
size_t find_connection_block_num_conf_bits(t_sram_orgz_info* cur_sram_orgz_info,
                                           const CircuitLibrary& circuit_lib,
                                           const MuxLibrary& mux_lib,
                                           const std::vector<t_switch_inf>& rr_switches,
                                           const RRGSB& rr_gsb,
                                           const t_rr_type& cb_type) {
  size_t num_conf_bits = 0;

  std::vector<enum e_side> cb_ipin_sides = rr_gsb.get_cb_ipin_sides(cb_type);
  for (size_t iside = 0; iside < cb_ipin_sides.size(); ++iside) {
    enum e_side cb_ipin_side = cb_ipin_sides[iside];
    for (size_t inode = 0; inode < rr_gsb.get_num_ipin_nodes(cb_ipin_side); ++inode) {
      /* Find the size of routing multiplexers driving this IPIN node */
      int mux_size = rr_gsb.get_ipin_node(cb_ipin_side, inode)->fan_in;
      /* Bypass fan_in == 1 or 0, they are not considered as routing multiplexers */
      if (2 > mux_size) {
        continue;
      }

      /* Get the circuit model id of the routing multiplexer */
      size_t switch_index = rr_gsb.get_ipin_node(cb_ipin_side, inode)->drive_switches[DEFAULT_SWITCH_ID];
      CircuitModelId mux_model = rr_switches[switch_index].circuit_model;

      /* Find the input size of the implementation of a routing multiplexer */
      size_t datapath_mux_size = rr_gsb.get_ipin_node(cb_ipin_side, inode)->fan_in;
      /* Get the multiplexing graph from the Mux Library */
      MuxId mux_id = mux_lib.mux_graph(mux_model, datapath_mux_size);
      const MuxGraph& mux_graph = mux_lib.mux_graph(mux_id);
      num_conf_bits += find_mux_num_config_bits(circuit_lib, mux_model, mux_graph, cur_sram_orgz_info->type);
    }
  }

  return num_conf_bits;
}


/*********************************************************************
 * Find the number of configuration bits of a Switch Block
 ********************************************************************/
size_t find_switch_block_num_conf_bits(t_sram_orgz_info* cur_sram_orgz_info,
                                       const CircuitLibrary& circuit_lib,
                                       const MuxLibrary& mux_lib,
                                       const std::vector<t_switch_inf>& rr_switches,
                                       const RRGSB& rr_gsb) {
  size_t num_conf_bits = 0;

  for (size_t side = 0; side < rr_gsb.get_num_sides(); ++side) {
    Side side_manager(side);
    for (size_t itrack = 0; itrack < rr_gsb.get_chan_width(side_manager.get_side()); ++itrack) {
      if (OUT_PORT != rr_gsb.get_chan_node_direction(side_manager.get_side(), itrack)) {
        continue;
      }
      /* Check if this node is just a passing wire */
      if (true == rr_gsb.is_sb_node_passing_wire(side_manager.get_side(), itrack)) {
        continue;
      }
      /* Check if this node has more than 2 drivers */
	  if (2 > rr_gsb.get_chan_node(side_manager.get_side(), itrack)->num_drive_rr_nodes) {
        continue;
      }
      /* Get the circuit model id of the routing multiplexer */
      size_t switch_index = rr_gsb.get_chan_node(side_manager.get_side(), itrack)->drive_switches[DEFAULT_SWITCH_ID];
      CircuitModelId mux_model = rr_switches[switch_index].circuit_model;

      /* Find the input size of the implementation of a routing multiplexer */
      size_t datapath_mux_size = rr_gsb.get_chan_node(side_manager.get_side(), itrack)->num_drive_rr_nodes;
      /* Get the multiplexing graph from the Mux Library */
      MuxId mux_id = mux_lib.mux_graph(mux_model, datapath_mux_size);
      const MuxGraph& mux_graph = mux_lib.mux_graph(mux_id);
      num_conf_bits += find_mux_num_config_bits(circuit_lib, mux_model, mux_graph, cur_sram_orgz_info->type);
    }
  }

  return num_conf_bits;
}

/*********************************************************************
 * Find the number of shared configuration bits of a Connection Block
 ********************************************************************/
size_t find_connection_block_num_shared_conf_bits(t_sram_orgz_info* cur_sram_orgz_info,
                                                  const CircuitLibrary& circuit_lib,
                                                  const MuxLibrary& mux_lib,
                                                  const std::vector<t_switch_inf>& rr_switches,
                                                  const RRGSB& rr_gsb,
                                                  const t_rr_type& cb_type) {
  size_t num_shared_conf_bits = 0;

  std::vector<enum e_side> cb_ipin_sides = rr_gsb.get_cb_ipin_sides(cb_type);
  for (size_t iside = 0; iside < cb_ipin_sides.size(); ++iside) {
    enum e_side cb_ipin_side = cb_ipin_sides[iside];
    for (size_t inode = 0; inode < rr_gsb.get_num_ipin_nodes(cb_ipin_side); ++inode) {
      /* Find the size of routing multiplexers driving this IPIN node */
      int mux_size = rr_gsb.get_ipin_node(cb_ipin_side, inode)->fan_in;
      /* Bypass fan_in == 1 or 0, they are not considered as routing multiplexers */
      if (2 > mux_size) {
        continue;
      }

      /* Get the circuit model id of the routing multiplexer */
      size_t switch_index = rr_gsb.get_ipin_node(cb_ipin_side, inode)->drive_switches[DEFAULT_SWITCH_ID];
      CircuitModelId mux_model = rr_switches[switch_index].circuit_model;

      /* Find the input size of the implementation of a routing multiplexer */
      size_t datapath_mux_size = rr_gsb.get_ipin_node(cb_ipin_side, inode)->fan_in;
      /* Get the multiplexing graph from the Mux Library */
      MuxId mux_id = mux_lib.mux_graph(mux_model, datapath_mux_size);
      const MuxGraph& mux_graph = mux_lib.mux_graph(mux_id);
      num_shared_conf_bits += find_mux_num_shared_config_bits(circuit_lib, mux_model, mux_graph, cur_sram_orgz_info->type);
    }
  }

  return num_shared_conf_bits;
}

/*********************************************************************
 * Find the number of shared configuration bits of a Switch Block
 ********************************************************************/
size_t find_switch_block_num_shared_conf_bits(t_sram_orgz_info* cur_sram_orgz_info,
                                              const CircuitLibrary& circuit_lib,
                                              const MuxLibrary& mux_lib,
                                              const std::vector<t_switch_inf>& rr_switches,
                                              const RRGSB& rr_gsb) {
  size_t num_shared_conf_bits = 0;

  for (size_t side = 0; side < rr_gsb.get_num_sides(); ++side) {
    Side side_manager(side);
    for (size_t itrack = 0; itrack < rr_gsb.get_chan_width(side_manager.get_side()); ++itrack) {
      if (OUT_PORT != rr_gsb.get_chan_node_direction(side_manager.get_side(), itrack)) {
        continue;
      }
      /* Check if this node is just a passing wire */
      if (true == rr_gsb.is_sb_node_passing_wire(side_manager.get_side(), itrack)) {
        continue;
      }
      /* Check if this node has more than 2 drivers */
	  if (2 > rr_gsb.get_chan_node(side_manager.get_side(), itrack)->num_drive_rr_nodes) {
        continue;
      }
      /* Get the circuit model id of the routing multiplexer */
      size_t switch_index = rr_gsb.get_chan_node(side_manager.get_side(), itrack)->drive_switches[DEFAULT_SWITCH_ID];
      CircuitModelId mux_model = rr_switches[switch_index].circuit_model;

      /* Find the input size of the implementation of a routing multiplexer */
      size_t datapath_mux_size = rr_gsb.get_chan_node(side_manager.get_side(), itrack)->num_drive_rr_nodes;
      /* Get the multiplexing graph from the Mux Library */
      MuxId mux_id = mux_lib.mux_graph(mux_model, datapath_mux_size);
      const MuxGraph& mux_graph = mux_lib.mux_graph(mux_id);
      num_shared_conf_bits += find_mux_num_shared_config_bits(circuit_lib, mux_model, mux_graph, cur_sram_orgz_info->type);
    }
  }

  return num_shared_conf_bits;
}

/********************************************************************
 * Find if a X-direction or Y-direction Connection Block contains
 * routing tracks only (zero configuration bits and routing multiplexers)
 *******************************************************************/
bool connection_block_contain_only_routing_tracks(const RRGSB& rr_gsb,
                                                  const t_rr_type& cb_type) {
  bool routing_track_only = true;

  /* Find routing multiplexers on the sides of a Connection block where IPIN nodes locate */
  std::vector<enum e_side> cb_sides = rr_gsb.get_cb_ipin_sides(cb_type);

  for (size_t side = 0; side < cb_sides.size(); ++side) {
    enum e_side cb_ipin_side = cb_sides[side];
    Side side_manager(cb_ipin_side);
    if (0 < rr_gsb.get_num_ipin_nodes(cb_ipin_side)) { 
      routing_track_only = false;
      break;
    }
  }
  
  return routing_track_only;
}
