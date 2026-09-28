
wire kernel_monitor_reset;
wire kernel_monitor_clock;
wire kernel_monitor_report;
assign kernel_monitor_reset = ~ap_rst_n;
assign kernel_monitor_clock = ap_clk;
assign kernel_monitor_report = 1'b0;
wire [5:0] axis_block_sigs;
wire [8:0] inst_idle_sigs;
wire [0:0] inst_block_sigs;
wire kernel_block;

assign axis_block_sigs[0] = ~grp_csi_feat_Pipeline_rd_fu_1024.csi_in_TDATA_blk_n;
assign axis_block_sigs[1] = ~grp_csi_feat_Pipeline_resync_fu_1041.csi_in_TDATA_blk_n;
assign axis_block_sigs[2] = ~grp_push_sample_fu_1098.grp_emit_windows_fu_232.grp_emit_windows_Pipeline_w_mot_fu_381.mot_TDATA_blk_n;
assign axis_block_sigs[3] = ~grp_push_sample_fu_1098.grp_emit_windows_fu_232.grp_emit_windows_Pipeline_w_brt_fu_398.brt_TDATA_blk_n;
assign axis_block_sigs[4] = ~grp_push_sample_fu_1098.grp_emit_windows_fu_232.grp_emit_windows_Pipeline_w_phs_fu_416.phs_TDATA_blk_n;
assign axis_block_sigs[5] = ~grp_push_sample_fu_1098.grp_emit_windows_fu_232.grp_emit_windows_Pipeline_w_tag_fu_434.tag_TDATA_blk_n;

assign inst_block_sigs[0] = 1'b0;

assign inst_idle_sigs[0] = 1'b0;
assign inst_idle_sigs[1] = grp_csi_feat_Pipeline_rd_fu_1024.ap_idle;
assign inst_idle_sigs[2] = grp_csi_feat_Pipeline_resync_fu_1041.ap_idle;
assign inst_idle_sigs[3] = grp_push_sample_fu_1098.ap_idle;
assign inst_idle_sigs[4] = grp_push_sample_fu_1098.grp_emit_windows_fu_232.ap_idle;
assign inst_idle_sigs[5] = grp_push_sample_fu_1098.grp_emit_windows_fu_232.grp_emit_windows_Pipeline_w_mot_fu_381.ap_idle;
assign inst_idle_sigs[6] = grp_push_sample_fu_1098.grp_emit_windows_fu_232.grp_emit_windows_Pipeline_w_brt_fu_398.ap_idle;
assign inst_idle_sigs[7] = grp_push_sample_fu_1098.grp_emit_windows_fu_232.grp_emit_windows_Pipeline_w_phs_fu_416.ap_idle;
assign inst_idle_sigs[8] = grp_push_sample_fu_1098.grp_emit_windows_fu_232.grp_emit_windows_Pipeline_w_tag_fu_434.ap_idle;

csi_feat_hls_deadlock_idx0_monitor csi_feat_hls_deadlock_idx0_monitor_U (
    .clock(kernel_monitor_clock),
    .reset(kernel_monitor_reset),
    .axis_block_sigs(axis_block_sigs),
    .inst_idle_sigs(inst_idle_sigs),
    .inst_block_sigs(inst_block_sigs),
    .block(kernel_block)
);


always @ (kernel_block or kernel_monitor_reset) begin
    if (kernel_block == 1'b1 && kernel_monitor_reset == 1'b0) begin
        find_kernel_block = 1'b1;
    end
    else begin
        find_kernel_block = 1'b0;
    end
end
