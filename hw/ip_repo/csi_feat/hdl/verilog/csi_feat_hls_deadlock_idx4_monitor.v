`timescale 1 ns / 1 ps

module csi_feat_hls_deadlock_idx4_monitor ( // for module csi_feat_csi_feat_inst.grp_push_sample_fu_1098.grp_emit_windows_fu_232
    input wire clock,
    input wire reset,
    input wire [5:0] axis_block_sigs,
    input wire [8:0] inst_idle_sigs,
    input wire [0:0] inst_block_sigs,
    output wire block
);

// signal declare
reg monitor_find_block;
wire idx5_block;
wire idx6_block;
wire idx7_block;
wire idx8_block;
wire sub_parallel_block;
wire all_sub_parallel_has_block;
wire all_sub_single_has_block;
wire cur_axis_has_block;
wire seq_is_axis_block;

assign block = monitor_find_block;
assign idx5_block = axis_block_sigs[2];
assign idx6_block = axis_block_sigs[3];
assign idx7_block = axis_block_sigs[4];
assign idx8_block = axis_block_sigs[5];
assign sub_parallel_block = 1'b0 | ((idx5_block & (axis_block_sigs[2])) & ((idx6_block | inst_idle_sigs[6]) & (idx7_block | inst_idle_sigs[7]))) | ((idx6_block & (axis_block_sigs[3])) & ((idx5_block | inst_idle_sigs[5]) & (idx7_block | inst_idle_sigs[7]))) | ((idx7_block & (axis_block_sigs[4])) & ((idx5_block | inst_idle_sigs[5]) & (idx6_block | inst_idle_sigs[6])));
assign all_sub_parallel_has_block = sub_parallel_block;
assign all_sub_single_has_block = 1'b0 | (idx8_block & (axis_block_sigs[5]));
assign cur_axis_has_block = 1'b0;
assign seq_is_axis_block = all_sub_parallel_has_block | all_sub_single_has_block | cur_axis_has_block;

always @(posedge clock) begin
    if (reset == 1'b1)
        monitor_find_block <= 1'b0;
    else if (seq_is_axis_block == 1'b1)
        monitor_find_block <= 1'b1;
    else
        monitor_find_block <= 1'b0;
end


// instant sub module
endmodule
