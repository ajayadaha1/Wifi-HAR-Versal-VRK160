// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
`timescale 1ns/1ps
(* DowngradeIPIdentifiedWarnings="yes" *) module csi_feat_control_s_axi
#(parameter
    C_S_AXI_ADDR_WIDTH = 7,
    C_S_AXI_DATA_WIDTH = 32
)(
    input  wire                          ACLK,
    input  wire                          ARESET,
    input  wire                          ACLK_EN,
    input  wire [C_S_AXI_ADDR_WIDTH-1:0] AWADDR,
    input  wire                          AWVALID,
    output wire                          AWREADY,
    input  wire [C_S_AXI_DATA_WIDTH-1:0] WDATA,
    input  wire [C_S_AXI_DATA_WIDTH/8-1:0] WSTRB,
    input  wire                          WVALID,
    output wire                          WREADY,
    output wire [1:0]                    BRESP,
    output wire                          BVALID,
    input  wire                          BREADY,
    input  wire [C_S_AXI_ADDR_WIDTH-1:0] ARADDR,
    input  wire                          ARVALID,
    output wire                          ARREADY,
    output wire [C_S_AXI_DATA_WIDTH-1:0] RDATA,
    output wire [1:0]                    RRESP,
    output wire                          RVALID,
    input  wire                          RREADY,
    output wire [31:0]                   ctrl,
    output wire [31:0]                   bin_us,
    output wire [31:0]                   hop,
    output wire [31:0]                   br_dec,
    output wire [31:0]                   k_ref,
    output wire [31:0]                   fs_nom,
    output wire [31:0]                   alpha,
    output wire [31:0]                   epoch,
    input  wire [31:0]                   n_in,
    input  wire [31:0]                   n_win,
    input  wire [31:0]                   n_samp
);
//------------------------Address Info-------------------
// Protocol Used: ap_ctrl_none
//
// 0x00 : reserved
// 0x04 : reserved
// 0x08 : reserved
// 0x0c : reserved
// 0x10 : Data signal of ctrl
//        bit 31~0 - ctrl[31:0] (Read/Write)
// 0x14 : reserved
// 0x18 : Data signal of bin_us
//        bit 31~0 - bin_us[31:0] (Read/Write)
// 0x1c : reserved
// 0x20 : Data signal of hop
//        bit 31~0 - hop[31:0] (Read/Write)
// 0x24 : reserved
// 0x28 : Data signal of br_dec
//        bit 31~0 - br_dec[31:0] (Read/Write)
// 0x2c : reserved
// 0x30 : Data signal of k_ref
//        bit 31~0 - k_ref[31:0] (Read/Write)
// 0x34 : reserved
// 0x38 : Data signal of fs_nom
//        bit 31~0 - fs_nom[31:0] (Read/Write)
// 0x3c : reserved
// 0x40 : Data signal of alpha
//        bit 31~0 - alpha[31:0] (Read/Write)
// 0x44 : reserved
// 0x48 : Data signal of epoch
//        bit 31~0 - epoch[31:0] (Read/Write)
// 0x4c : reserved
// 0x50 : Data signal of n_in
//        bit 31~0 - n_in[31:0] (Read)
// 0x54 : reserved
// 0x60 : Data signal of n_win
//        bit 31~0 - n_win[31:0] (Read)
// 0x64 : reserved
// 0x70 : Data signal of n_samp
//        bit 31~0 - n_samp[31:0] (Read)
// 0x74 : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

//------------------------Parameter----------------------
localparam
    ADDR_CTRL_DATA_0   = 7'h10,
    ADDR_CTRL_CTRL     = 7'h14,
    ADDR_BIN_US_DATA_0 = 7'h18,
    ADDR_BIN_US_CTRL   = 7'h1c,
    ADDR_HOP_DATA_0    = 7'h20,
    ADDR_HOP_CTRL      = 7'h24,
    ADDR_BR_DEC_DATA_0 = 7'h28,
    ADDR_BR_DEC_CTRL   = 7'h2c,
    ADDR_K_REF_DATA_0  = 7'h30,
    ADDR_K_REF_CTRL    = 7'h34,
    ADDR_FS_NOM_DATA_0 = 7'h38,
    ADDR_FS_NOM_CTRL   = 7'h3c,
    ADDR_ALPHA_DATA_0  = 7'h40,
    ADDR_ALPHA_CTRL    = 7'h44,
    ADDR_EPOCH_DATA_0  = 7'h48,
    ADDR_EPOCH_CTRL    = 7'h4c,
    ADDR_N_IN_DATA_0   = 7'h50,
    ADDR_N_IN_CTRL     = 7'h54,
    ADDR_N_WIN_DATA_0  = 7'h60,
    ADDR_N_WIN_CTRL    = 7'h64,
    ADDR_N_SAMP_DATA_0 = 7'h70,
    ADDR_N_SAMP_CTRL   = 7'h74,
    WRIDLE             = 2'd0,
    WRDATA             = 2'd1,
    WRRESP             = 2'd2,
    WRRESET            = 2'd3,
    RDIDLE             = 2'd0,
    RDDATA             = 2'd1,
    RDRESET            = 2'd2,
    ADDR_BITS                = 7;

//------------------------Local signal-------------------
    reg  [1:0]                    wstate = WRRESET;
    reg  [1:0]                    wnext;
    reg  [ADDR_BITS-1:0]          waddr;
    wire [C_S_AXI_DATA_WIDTH-1:0] wmask;
    wire                          aw_hs;
    wire                          w_hs;
    reg  [1:0]                    rstate = RDRESET;
    reg  [1:0]                    rnext;
    reg  [C_S_AXI_DATA_WIDTH-1:0] rdata;
    wire                          ar_hs;
    wire [ADDR_BITS-1:0]          raddr;
    // internal registers
    reg  [31:0]                   int_ctrl = 'b0;
    reg  [31:0]                   int_bin_us = 'b0;
    reg  [31:0]                   int_hop = 'b0;
    reg  [31:0]                   int_br_dec = 'b0;
    reg  [31:0]                   int_k_ref = 'b0;
    reg  [31:0]                   int_fs_nom = 'b0;
    reg  [31:0]                   int_alpha = 'b0;
    reg  [31:0]                   int_epoch = 'b0;
    reg  [31:0]                   int_n_in = 'b0;
    reg  [31:0]                   int_n_win = 'b0;
    reg  [31:0]                   int_n_samp = 'b0;

//------------------------Instantiation------------------


//------------------------AXI write fsm------------------
assign AWREADY = (wstate == WRIDLE);
assign WREADY  = (wstate == WRDATA);
assign BVALID  = (wstate == WRRESP);
assign BRESP   = 2'b00;  // OKAY
assign wmask   = { {8{WSTRB[3]}}, {8{WSTRB[2]}}, {8{WSTRB[1]}}, {8{WSTRB[0]}} };
assign aw_hs   = AWVALID & AWREADY;
assign w_hs    = WVALID & WREADY;

// wstate
always @(posedge ACLK) begin
    if (ARESET)
        wstate <= WRRESET;
    else if (ACLK_EN)
        wstate <= wnext;
end

// wnext
always @(*) begin
    case (wstate)
        WRIDLE:
            if (AWVALID)
                wnext = WRDATA;
            else
                wnext = WRIDLE;
        WRDATA:
            if (WVALID)
                wnext = WRRESP;
            else
                wnext = WRDATA;
        WRRESP:
            if (BREADY & BVALID)
                wnext = WRIDLE;
            else
                wnext = WRRESP;
        default:
            wnext = WRIDLE;
    endcase
end

// waddr
always @(posedge ACLK) begin
    if (ACLK_EN) begin
        if (aw_hs)
            waddr <= {AWADDR[ADDR_BITS-1:2], {2{1'b0}}};
    end
end

//------------------------AXI read fsm-------------------
assign ARREADY = (rstate == RDIDLE);
assign RDATA   = rdata;
assign RRESP   = 2'b00;  // OKAY
assign RVALID  = (rstate == RDDATA);
assign ar_hs   = ARVALID & ARREADY;
assign raddr   = ARADDR[ADDR_BITS-1:0];

// rstate
always @(posedge ACLK) begin
    if (ARESET)
        rstate <= RDRESET;
    else if (ACLK_EN)
        rstate <= rnext;
end

// rnext
always @(*) begin
    case (rstate)
        RDIDLE:
            if (ARVALID)
                rnext = RDDATA;
            else
                rnext = RDIDLE;
        RDDATA:
            if (RREADY & RVALID)
                rnext = RDIDLE;
            else
                rnext = RDDATA;
        default:
            rnext = RDIDLE;
    endcase
end

// rdata
always @(posedge ACLK) begin
    if (ACLK_EN) begin
        if (ar_hs) begin
            rdata <= 'b0;
            case (raddr)
                ADDR_CTRL_DATA_0: begin
                    rdata <= int_ctrl[31:0];
                end
                ADDR_BIN_US_DATA_0: begin
                    rdata <= int_bin_us[31:0];
                end
                ADDR_HOP_DATA_0: begin
                    rdata <= int_hop[31:0];
                end
                ADDR_BR_DEC_DATA_0: begin
                    rdata <= int_br_dec[31:0];
                end
                ADDR_K_REF_DATA_0: begin
                    rdata <= int_k_ref[31:0];
                end
                ADDR_FS_NOM_DATA_0: begin
                    rdata <= int_fs_nom[31:0];
                end
                ADDR_ALPHA_DATA_0: begin
                    rdata <= int_alpha[31:0];
                end
                ADDR_EPOCH_DATA_0: begin
                    rdata <= int_epoch[31:0];
                end
                ADDR_N_IN_DATA_0: begin
                    rdata <= int_n_in[31:0];
                end
                ADDR_N_WIN_DATA_0: begin
                    rdata <= int_n_win[31:0];
                end
                ADDR_N_SAMP_DATA_0: begin
                    rdata <= int_n_samp[31:0];
                end
            endcase
        end
    end
end


//------------------------Register logic-----------------
assign ctrl   = int_ctrl;
assign bin_us = int_bin_us;
assign hop    = int_hop;
assign br_dec = int_br_dec;
assign k_ref  = int_k_ref;
assign fs_nom = int_fs_nom;
assign alpha  = int_alpha;
assign epoch  = int_epoch;
// int_ctrl[31:0]
always @(posedge ACLK) begin
    if (ARESET)
        int_ctrl[31:0] <= 0;
    else if (ACLK_EN) begin
        if (w_hs && waddr == ADDR_CTRL_DATA_0)
            int_ctrl[31:0] <= (WDATA[31:0] & wmask) | (int_ctrl[31:0] & ~wmask);
    end
end

// int_bin_us[31:0]
always @(posedge ACLK) begin
    if (ARESET)
        int_bin_us[31:0] <= 0;
    else if (ACLK_EN) begin
        if (w_hs && waddr == ADDR_BIN_US_DATA_0)
            int_bin_us[31:0] <= (WDATA[31:0] & wmask) | (int_bin_us[31:0] & ~wmask);
    end
end

// int_hop[31:0]
always @(posedge ACLK) begin
    if (ARESET)
        int_hop[31:0] <= 0;
    else if (ACLK_EN) begin
        if (w_hs && waddr == ADDR_HOP_DATA_0)
            int_hop[31:0] <= (WDATA[31:0] & wmask) | (int_hop[31:0] & ~wmask);
    end
end

// int_br_dec[31:0]
always @(posedge ACLK) begin
    if (ARESET)
        int_br_dec[31:0] <= 0;
    else if (ACLK_EN) begin
        if (w_hs && waddr == ADDR_BR_DEC_DATA_0)
            int_br_dec[31:0] <= (WDATA[31:0] & wmask) | (int_br_dec[31:0] & ~wmask);
    end
end

// int_k_ref[31:0]
always @(posedge ACLK) begin
    if (ARESET)
        int_k_ref[31:0] <= 0;
    else if (ACLK_EN) begin
        if (w_hs && waddr == ADDR_K_REF_DATA_0)
            int_k_ref[31:0] <= (WDATA[31:0] & wmask) | (int_k_ref[31:0] & ~wmask);
    end
end

// int_fs_nom[31:0]
always @(posedge ACLK) begin
    if (ARESET)
        int_fs_nom[31:0] <= 0;
    else if (ACLK_EN) begin
        if (w_hs && waddr == ADDR_FS_NOM_DATA_0)
            int_fs_nom[31:0] <= (WDATA[31:0] & wmask) | (int_fs_nom[31:0] & ~wmask);
    end
end

// int_alpha[31:0]
always @(posedge ACLK) begin
    if (ARESET)
        int_alpha[31:0] <= 0;
    else if (ACLK_EN) begin
        if (w_hs && waddr == ADDR_ALPHA_DATA_0)
            int_alpha[31:0] <= (WDATA[31:0] & wmask) | (int_alpha[31:0] & ~wmask);
    end
end

// int_epoch[31:0]
always @(posedge ACLK) begin
    if (ARESET)
        int_epoch[31:0] <= 0;
    else if (ACLK_EN) begin
        if (w_hs && waddr == ADDR_EPOCH_DATA_0)
            int_epoch[31:0] <= (WDATA[31:0] & wmask) | (int_epoch[31:0] & ~wmask);
    end
end

// int_n_in
always @(posedge ACLK) begin
    if (ARESET)
        int_n_in <= 0;
    else if (ACLK_EN) begin
            int_n_in <= n_in;
    end
end

// int_n_win
always @(posedge ACLK) begin
    if (ARESET)
        int_n_win <= 0;
    else if (ACLK_EN) begin
            int_n_win <= n_win;
    end
end

// int_n_samp
always @(posedge ACLK) begin
    if (ARESET)
        int_n_samp <= 0;
    else if (ACLK_EN) begin
            int_n_samp <= n_samp;
    end
end


//------------------------Memory logic-------------------

endmodule
