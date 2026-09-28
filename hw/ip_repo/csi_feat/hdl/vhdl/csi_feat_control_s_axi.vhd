-- ==============================================================
-- Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
-- Tool Version Limit: 2025.11
-- Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
-- Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
-- 
-- ==============================================================
library IEEE;
use IEEE.STD_LOGIC_1164.all;
use IEEE.NUMERIC_STD.all;

entity csi_feat_control_s_axi is
generic (
    C_S_AXI_ADDR_WIDTH    : INTEGER := 7;
    C_S_AXI_DATA_WIDTH    : INTEGER := 32);
port (
    ACLK                  :in   STD_LOGIC;
    ARESET                :in   STD_LOGIC;
    ACLK_EN               :in   STD_LOGIC;
    AWADDR                :in   STD_LOGIC_VECTOR(C_S_AXI_ADDR_WIDTH-1 downto 0);
    AWVALID               :in   STD_LOGIC;
    AWREADY               :out  STD_LOGIC;
    WDATA                 :in   STD_LOGIC_VECTOR(C_S_AXI_DATA_WIDTH-1 downto 0);
    WSTRB                 :in   STD_LOGIC_VECTOR(C_S_AXI_DATA_WIDTH/8-1 downto 0);
    WVALID                :in   STD_LOGIC;
    WREADY                :out  STD_LOGIC;
    BRESP                 :out  STD_LOGIC_VECTOR(1 downto 0);
    BVALID                :out  STD_LOGIC;
    BREADY                :in   STD_LOGIC;
    ARADDR                :in   STD_LOGIC_VECTOR(C_S_AXI_ADDR_WIDTH-1 downto 0);
    ARVALID               :in   STD_LOGIC;
    ARREADY               :out  STD_LOGIC;
    RDATA                 :out  STD_LOGIC_VECTOR(C_S_AXI_DATA_WIDTH-1 downto 0);
    RRESP                 :out  STD_LOGIC_VECTOR(1 downto 0);
    RVALID                :out  STD_LOGIC;
    RREADY                :in   STD_LOGIC;
    ctrl                  :out  STD_LOGIC_VECTOR(31 downto 0);
    bin_us                :out  STD_LOGIC_VECTOR(31 downto 0);
    hop                   :out  STD_LOGIC_VECTOR(31 downto 0);
    br_dec                :out  STD_LOGIC_VECTOR(31 downto 0);
    k_ref                 :out  STD_LOGIC_VECTOR(31 downto 0);
    fs_nom                :out  STD_LOGIC_VECTOR(31 downto 0);
    alpha                 :out  STD_LOGIC_VECTOR(31 downto 0);
    epoch                 :out  STD_LOGIC_VECTOR(31 downto 0);
    n_in                  :in   STD_LOGIC_VECTOR(31 downto 0);
    n_win                 :in   STD_LOGIC_VECTOR(31 downto 0);
    n_samp                :in   STD_LOGIC_VECTOR(31 downto 0)
);
end entity csi_feat_control_s_axi;

-- ------------------------Address Info-------------------
-- Protocol Used: ap_ctrl_none
--
-- 0x00 : reserved
-- 0x04 : reserved
-- 0x08 : reserved
-- 0x0c : reserved
-- 0x10 : Data signal of ctrl
--        bit 31~0 - ctrl[31:0] (Read/Write)
-- 0x14 : reserved
-- 0x18 : Data signal of bin_us
--        bit 31~0 - bin_us[31:0] (Read/Write)
-- 0x1c : reserved
-- 0x20 : Data signal of hop
--        bit 31~0 - hop[31:0] (Read/Write)
-- 0x24 : reserved
-- 0x28 : Data signal of br_dec
--        bit 31~0 - br_dec[31:0] (Read/Write)
-- 0x2c : reserved
-- 0x30 : Data signal of k_ref
--        bit 31~0 - k_ref[31:0] (Read/Write)
-- 0x34 : reserved
-- 0x38 : Data signal of fs_nom
--        bit 31~0 - fs_nom[31:0] (Read/Write)
-- 0x3c : reserved
-- 0x40 : Data signal of alpha
--        bit 31~0 - alpha[31:0] (Read/Write)
-- 0x44 : reserved
-- 0x48 : Data signal of epoch
--        bit 31~0 - epoch[31:0] (Read/Write)
-- 0x4c : reserved
-- 0x50 : Data signal of n_in
--        bit 31~0 - n_in[31:0] (Read)
-- 0x54 : reserved
-- 0x60 : Data signal of n_win
--        bit 31~0 - n_win[31:0] (Read)
-- 0x64 : reserved
-- 0x70 : Data signal of n_samp
--        bit 31~0 - n_samp[31:0] (Read)
-- 0x74 : reserved
-- (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

architecture behave of csi_feat_control_s_axi is
attribute DowngradeIPIdentifiedWarnings : STRING;
attribute DowngradeIPIdentifiedWarnings of behave : architecture is "yes";
    type states is (wridle, wrdata, wrresp, wrreset, rdidle, rddata, rdreset);  -- read and write fsm states
    signal wstate  : states := wrreset;
    signal rstate  : states := rdreset;
    signal wnext, rnext: states;
    constant ADDR_CTRL_DATA_0   : INTEGER := 16#10#;
    constant ADDR_CTRL_CTRL     : INTEGER := 16#14#;
    constant ADDR_BIN_US_DATA_0 : INTEGER := 16#18#;
    constant ADDR_BIN_US_CTRL   : INTEGER := 16#1c#;
    constant ADDR_HOP_DATA_0    : INTEGER := 16#20#;
    constant ADDR_HOP_CTRL      : INTEGER := 16#24#;
    constant ADDR_BR_DEC_DATA_0 : INTEGER := 16#28#;
    constant ADDR_BR_DEC_CTRL   : INTEGER := 16#2c#;
    constant ADDR_K_REF_DATA_0  : INTEGER := 16#30#;
    constant ADDR_K_REF_CTRL    : INTEGER := 16#34#;
    constant ADDR_FS_NOM_DATA_0 : INTEGER := 16#38#;
    constant ADDR_FS_NOM_CTRL   : INTEGER := 16#3c#;
    constant ADDR_ALPHA_DATA_0  : INTEGER := 16#40#;
    constant ADDR_ALPHA_CTRL    : INTEGER := 16#44#;
    constant ADDR_EPOCH_DATA_0  : INTEGER := 16#48#;
    constant ADDR_EPOCH_CTRL    : INTEGER := 16#4c#;
    constant ADDR_N_IN_DATA_0   : INTEGER := 16#50#;
    constant ADDR_N_IN_CTRL     : INTEGER := 16#54#;
    constant ADDR_N_WIN_DATA_0  : INTEGER := 16#60#;
    constant ADDR_N_WIN_CTRL    : INTEGER := 16#64#;
    constant ADDR_N_SAMP_DATA_0 : INTEGER := 16#70#;
    constant ADDR_N_SAMP_CTRL   : INTEGER := 16#74#;
    constant ADDR_BITS         : INTEGER := 7;

    signal AWREADY_t           : STD_LOGIC;
    signal WREADY_t            : STD_LOGIC;
    signal ARREADY_t           : STD_LOGIC;
    signal RVALID_t            : STD_LOGIC;
    signal BVALID_t            : STD_LOGIC;
    signal waddr               : UNSIGNED(ADDR_BITS-1 downto 0);
    signal wmask               : UNSIGNED(C_S_AXI_DATA_WIDTH-1 downto 0);
    signal aw_hs               : STD_LOGIC;
    signal w_hs                : STD_LOGIC;
    signal rdata_data          : UNSIGNED(C_S_AXI_DATA_WIDTH-1 downto 0);
    signal ar_hs               : STD_LOGIC;
    signal raddr               : UNSIGNED(ADDR_BITS-1 downto 0);
    -- internal registers
    signal int_ctrl            : UNSIGNED(31 downto 0) := (others => '0');
    signal int_bin_us          : UNSIGNED(31 downto 0) := (others => '0');
    signal int_hop             : UNSIGNED(31 downto 0) := (others => '0');
    signal int_br_dec          : UNSIGNED(31 downto 0) := (others => '0');
    signal int_k_ref           : UNSIGNED(31 downto 0) := (others => '0');
    signal int_fs_nom          : UNSIGNED(31 downto 0) := (others => '0');
    signal int_alpha           : UNSIGNED(31 downto 0) := (others => '0');
    signal int_epoch           : UNSIGNED(31 downto 0) := (others => '0');
    signal int_n_in            : UNSIGNED(31 downto 0) := (others => '0');
    signal int_n_win           : UNSIGNED(31 downto 0) := (others => '0');
    signal int_n_samp          : UNSIGNED(31 downto 0) := (others => '0');


begin
-- ----------------------- Instantiation------------------


-- ----------------------- AXI WRITE ---------------------
    AWREADY_t <=  '1' when wstate = wridle else '0';
    AWREADY   <=  AWREADY_t;
    WREADY_t  <=  '1' when wstate = wrdata else '0';
    WREADY    <=  WREADY_t;
    BVALID_t  <=  '1' when wstate = wrresp else '0';
    BVALID    <=  BVALID_t;
    BRESP     <=  "00";  -- OKAY
    wmask     <=  (31 downto 24 => WSTRB(3), 23 downto 16 => WSTRB(2), 15 downto 8 => WSTRB(1), 7 downto 0 => WSTRB(0));
    aw_hs     <=  AWVALID and AWREADY_t;
    w_hs      <=  WVALID and WREADY_t;

    -- write FSM
    process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ARESET = '1') then
                wstate <= wrreset;
            elsif (ACLK_EN = '1') then
                wstate <= wnext;
            end if;
        end if;
    end process;

    process (wstate, AWVALID, WVALID, BREADY, BVALID_t)
    begin
        case (wstate) is
        when wridle =>
            if (AWVALID = '1') then
                wnext <= wrdata;
            else
                wnext <= wridle;
            end if;
        when wrdata =>
            if (WVALID = '1') then
                wnext <= wrresp;
            else
                wnext <= wrdata;
            end if;
        when wrresp =>
            if (BREADY = '1' and BVALID_t = '1') then
                wnext <= wridle;
            else
                wnext <= wrresp;
            end if;
        when others =>
            wnext <= wridle;
        end case;
    end process;

    waddr_proc : process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ACLK_EN = '1') then
                if (aw_hs = '1') then
                    waddr <= UNSIGNED(AWADDR(ADDR_BITS-1 downto 2) & (1 downto 0 => '0'));
                end if;
            end if;
        end if;
    end process;

-- ----------------------- AXI READ ----------------------
    ARREADY_t <= '1' when (rstate = rdidle) else '0';
    ARREADY <= ARREADY_t;
    RDATA   <= STD_LOGIC_VECTOR(rdata_data);
    RRESP   <= "00";  -- OKAY
    RVALID_t  <= '1' when (rstate = rddata) else '0';
    RVALID    <= RVALID_t;
    ar_hs   <= ARVALID and ARREADY_t;
    raddr   <= UNSIGNED(ARADDR(ADDR_BITS-1 downto 0));

    -- read FSM
    process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ARESET = '1') then
                rstate <= rdreset;
            elsif (ACLK_EN = '1') then
                rstate <= rnext;
            end if;
        end if;
    end process;

    process (rstate, ARVALID, RREADY, RVALID_t)
    begin
        case (rstate) is
        when rdidle =>
            if (ARVALID = '1') then
                rnext <= rddata;
            else
                rnext <= rdidle;
            end if;
        when rddata =>
            if (RREADY = '1' and RVALID_t = '1') then
                rnext <= rdidle;
            else
                rnext <= rddata;
            end if;
        when others =>
            rnext <= rdidle;
        end case;
    end process;

    rdata_proc : process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ACLK_EN = '1') then
                if (ar_hs = '1') then
                    rdata_data <= (others => '0');
                    case (TO_INTEGER(raddr)) is
                    when ADDR_CTRL_DATA_0 =>
                        rdata_data <= RESIZE(int_ctrl(31 downto 0), 32);
                    when ADDR_BIN_US_DATA_0 =>
                        rdata_data <= RESIZE(int_bin_us(31 downto 0), 32);
                    when ADDR_HOP_DATA_0 =>
                        rdata_data <= RESIZE(int_hop(31 downto 0), 32);
                    when ADDR_BR_DEC_DATA_0 =>
                        rdata_data <= RESIZE(int_br_dec(31 downto 0), 32);
                    when ADDR_K_REF_DATA_0 =>
                        rdata_data <= RESIZE(int_k_ref(31 downto 0), 32);
                    when ADDR_FS_NOM_DATA_0 =>
                        rdata_data <= RESIZE(int_fs_nom(31 downto 0), 32);
                    when ADDR_ALPHA_DATA_0 =>
                        rdata_data <= RESIZE(int_alpha(31 downto 0), 32);
                    when ADDR_EPOCH_DATA_0 =>
                        rdata_data <= RESIZE(int_epoch(31 downto 0), 32);
                    when ADDR_N_IN_DATA_0 =>
                        rdata_data <= RESIZE(int_n_in(31 downto 0), 32);
                    when ADDR_N_WIN_DATA_0 =>
                        rdata_data <= RESIZE(int_n_win(31 downto 0), 32);
                    when ADDR_N_SAMP_DATA_0 =>
                        rdata_data <= RESIZE(int_n_samp(31 downto 0), 32);
                    when others =>
                        NULL;
                    end case;
                end if;
            end if;
        end if;
    end process;

-- ----------------------- Register logic ----------------
    ctrl                 <= STD_LOGIC_VECTOR(int_ctrl);
    bin_us               <= STD_LOGIC_VECTOR(int_bin_us);
    hop                  <= STD_LOGIC_VECTOR(int_hop);
    br_dec               <= STD_LOGIC_VECTOR(int_br_dec);
    k_ref                <= STD_LOGIC_VECTOR(int_k_ref);
    fs_nom               <= STD_LOGIC_VECTOR(int_fs_nom);
    alpha                <= STD_LOGIC_VECTOR(int_alpha);
    epoch                <= STD_LOGIC_VECTOR(int_epoch);

    process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ARESET = '1') then
                int_ctrl(31 downto 0) <= (others => '0');
            elsif (ACLK_EN = '1') then
                if (w_hs = '1' and waddr = ADDR_CTRL_DATA_0) then
                    int_ctrl(31 downto 0) <= (UNSIGNED(WDATA(31 downto 0)) and wmask(31 downto 0)) or ((not wmask(31 downto 0)) and int_ctrl(31 downto 0));
                end if;
            end if;
        end if;
    end process;

    process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ARESET = '1') then
                int_bin_us(31 downto 0) <= (others => '0');
            elsif (ACLK_EN = '1') then
                if (w_hs = '1' and waddr = ADDR_BIN_US_DATA_0) then
                    int_bin_us(31 downto 0) <= (UNSIGNED(WDATA(31 downto 0)) and wmask(31 downto 0)) or ((not wmask(31 downto 0)) and int_bin_us(31 downto 0));
                end if;
            end if;
        end if;
    end process;

    process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ARESET = '1') then
                int_hop(31 downto 0) <= (others => '0');
            elsif (ACLK_EN = '1') then
                if (w_hs = '1' and waddr = ADDR_HOP_DATA_0) then
                    int_hop(31 downto 0) <= (UNSIGNED(WDATA(31 downto 0)) and wmask(31 downto 0)) or ((not wmask(31 downto 0)) and int_hop(31 downto 0));
                end if;
            end if;
        end if;
    end process;

    process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ARESET = '1') then
                int_br_dec(31 downto 0) <= (others => '0');
            elsif (ACLK_EN = '1') then
                if (w_hs = '1' and waddr = ADDR_BR_DEC_DATA_0) then
                    int_br_dec(31 downto 0) <= (UNSIGNED(WDATA(31 downto 0)) and wmask(31 downto 0)) or ((not wmask(31 downto 0)) and int_br_dec(31 downto 0));
                end if;
            end if;
        end if;
    end process;

    process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ARESET = '1') then
                int_k_ref(31 downto 0) <= (others => '0');
            elsif (ACLK_EN = '1') then
                if (w_hs = '1' and waddr = ADDR_K_REF_DATA_0) then
                    int_k_ref(31 downto 0) <= (UNSIGNED(WDATA(31 downto 0)) and wmask(31 downto 0)) or ((not wmask(31 downto 0)) and int_k_ref(31 downto 0));
                end if;
            end if;
        end if;
    end process;

    process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ARESET = '1') then
                int_fs_nom(31 downto 0) <= (others => '0');
            elsif (ACLK_EN = '1') then
                if (w_hs = '1' and waddr = ADDR_FS_NOM_DATA_0) then
                    int_fs_nom(31 downto 0) <= (UNSIGNED(WDATA(31 downto 0)) and wmask(31 downto 0)) or ((not wmask(31 downto 0)) and int_fs_nom(31 downto 0));
                end if;
            end if;
        end if;
    end process;

    process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ARESET = '1') then
                int_alpha(31 downto 0) <= (others => '0');
            elsif (ACLK_EN = '1') then
                if (w_hs = '1' and waddr = ADDR_ALPHA_DATA_0) then
                    int_alpha(31 downto 0) <= (UNSIGNED(WDATA(31 downto 0)) and wmask(31 downto 0)) or ((not wmask(31 downto 0)) and int_alpha(31 downto 0));
                end if;
            end if;
        end if;
    end process;

    process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ARESET = '1') then
                int_epoch(31 downto 0) <= (others => '0');
            elsif (ACLK_EN = '1') then
                if (w_hs = '1' and waddr = ADDR_EPOCH_DATA_0) then
                    int_epoch(31 downto 0) <= (UNSIGNED(WDATA(31 downto 0)) and wmask(31 downto 0)) or ((not wmask(31 downto 0)) and int_epoch(31 downto 0));
                end if;
            end if;
        end if;
    end process;

    process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ARESET = '1') then
                int_n_in <= (others => '0');
            elsif (ACLK_EN = '1') then
                if (true) then
                    int_n_in <= UNSIGNED(n_in);
                end if;
            end if;
        end if;
    end process;

    process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ARESET = '1') then
                int_n_win <= (others => '0');
            elsif (ACLK_EN = '1') then
                if (true) then
                    int_n_win <= UNSIGNED(n_win);
                end if;
            end if;
        end if;
    end process;

    process (ACLK)
    begin
        if (ACLK'event and ACLK = '1') then
            if (ARESET = '1') then
                int_n_samp <= (others => '0');
            elsif (ACLK_EN = '1') then
                if (true) then
                    int_n_samp <= UNSIGNED(n_samp);
                end if;
            end if;
        end if;
    end process;


-- ----------------------- Memory logic ------------------

end architecture behave;
