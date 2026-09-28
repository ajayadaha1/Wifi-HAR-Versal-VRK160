// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
`timescale 1ns/1ps
(* DowngradeIPIdentifiedWarnings="yes" *) module ofdm_fe_control_s_axi
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
    output wire [31:0]                   thr2,
    output wire [31:0]                   p_min,
    output wire [31:0]                   n_plat,
    output wire [31:0]                   holdoff,
    output wire [31:0]                   epoch,
    input  wire [31:0]                   n_trig,
    input  wire [31:0]                   p_now
);
//------------------------Address Info-------------------
// Protocol Used: ap_ctrl_none
//
// 0x00 : reserved
// 0x04 : reserved
// 0x08 : reserved
// 0x0c : reserved
// 0x10 : Data signal of thr2
//        bit 31~0 - thr2[31:0] (Read/Write)
// 0x14 : reserved
// 0x18 : Data signal of p_min
//        bit 31~0 - p_min[31:0] (Read/Write)
// 0x1c : reserved
// 0x20 : Data signal of n_plat
//        bit 31~0 - n_plat[31:0] (Read/Write)
// 0x24 : reserved
// 0x28 : Data signal of holdoff
//        bit 31~0 - holdoff[31:0] (Read/Write)
// 0x2c : reserved
// 0x30 : Data signal of epoch
//        bit 31~0 - epoch[31:0] (Read/Write)
// 0x34 : reserved
// 0x38 : Data signal of n_trig
//        bit 31~0 - n_trig[31:0] (Read)
// 0x3c : reserved
// 0x48 : Data signal of p_now
//        bit 31~0 - p_now[31:0] (Read)
// 0x4c : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

//------------------------Parameter----------------------
localparam
    ADDR_THR2_DATA_0    = 7'h10,
    ADDR_THR2_CTRL      = 7'h14,
    ADDR_P_MIN_DATA_0   = 7'h18,
    ADDR_P_MIN_CTRL     = 7'h1c,
    ADDR_N_PLAT_DATA_0  = 7'h20,
    ADDR_N_PLAT_CTRL    = 7'h24,
    ADDR_HOLDOFF_DATA_0 = 7'h28,
    ADDR_HOLDOFF_CTRL   = 7'h2c,
    ADDR_EPOCH_DATA_0   = 7'h30,
    ADDR_EPOCH_CTRL     = 7'h34,
    ADDR_N_TRIG_DATA_0  = 7'h38,
    ADDR_N_TRIG_CTRL    = 7'h3c,
    ADDR_P_NOW_DATA_0   = 7'h48,
    ADDR_P_NOW_CTRL     = 7'h4c,
    WRIDLE              = 2'd0,
    WRDATA              = 2'd1,
    WRRESP              = 2'd2,
    WRRESET             = 2'd3,
    RDIDLE              = 2'd0,
    RDDATA              = 2'd1,
    RDRESET             = 2'd2,
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
    reg  [31:0]                   int_thr2 = 'b0;
    reg  [31:0]                   int_p_min = 'b0;
    reg  [31:0]                   int_n_plat = 'b0;
    reg  [31:0]                   int_holdoff = 'b0;
    reg  [31:0]                   int_epoch = 'b0;
    reg  [31:0]                   int_n_trig = 'b0;
    reg  [31:0]                   int_p_now = 'b0;

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
                ADDR_THR2_DATA_0: begin
                    rdata <= int_thr2[31:0];
                end
                ADDR_P_MIN_DATA_0: begin
                    rdata <= int_p_min[31:0];
                end
                ADDR_N_PLAT_DATA_0: begin
                    rdata <= int_n_plat[31:0];
                end
                ADDR_HOLDOFF_DATA_0: begin
                    rdata <= int_holdoff[31:0];
                end
                ADDR_EPOCH_DATA_0: begin
                    rdata <= int_epoch[31:0];
                end
                ADDR_N_TRIG_DATA_0: begin
                    rdata <= int_n_trig[31:0];
                end
                ADDR_P_NOW_DATA_0: begin
                    rdata <= int_p_now[31:0];
                end
            endcase
        end
    end
end


//------------------------Register logic-----------------
assign thr2    = int_thr2;
assign p_min   = int_p_min;
assign n_plat  = int_n_plat;
assign holdoff = int_holdoff;
assign epoch   = int_epoch;
// int_thr2[31:0]
always @(posedge ACLK) begin
    if (ARESET)
        int_thr2[31:0] <= 0;
    else if (ACLK_EN) begin
        if (w_hs && waddr == ADDR_THR2_DATA_0)
            int_thr2[31:0] <= (WDATA[31:0] & wmask) | (int_thr2[31:0] & ~wmask);
    end
end

// int_p_min[31:0]
always @(posedge ACLK) begin
    if (ARESET)
        int_p_min[31:0] <= 0;
    else if (ACLK_EN) begin
        if (w_hs && waddr == ADDR_P_MIN_DATA_0)
            int_p_min[31:0] <= (WDATA[31:0] & wmask) | (int_p_min[31:0] & ~wmask);
    end
end

// int_n_plat[31:0]
always @(posedge ACLK) begin
    if (ARESET)
        int_n_plat[31:0] <= 0;
    else if (ACLK_EN) begin
        if (w_hs && waddr == ADDR_N_PLAT_DATA_0)
            int_n_plat[31:0] <= (WDATA[31:0] & wmask) | (int_n_plat[31:0] & ~wmask);
    end
end

// int_holdoff[31:0]
always @(posedge ACLK) begin
    if (ARESET)
        int_holdoff[31:0] <= 0;
    else if (ACLK_EN) begin
        if (w_hs && waddr == ADDR_HOLDOFF_DATA_0)
            int_holdoff[31:0] <= (WDATA[31:0] & wmask) | (int_holdoff[31:0] & ~wmask);
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

// int_n_trig
always @(posedge ACLK) begin
    if (ARESET)
        int_n_trig <= 0;
    else if (ACLK_EN) begin
            int_n_trig <= n_trig;
    end
end

// int_p_now
always @(posedge ACLK) begin
    if (ARESET)
        int_p_now <= 0;
    else if (ACLK_EN) begin
            int_p_now <= p_now;
    end
end


//------------------------Memory logic-------------------

endmodule
