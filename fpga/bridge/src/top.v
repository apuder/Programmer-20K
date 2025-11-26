module bridge(
	input       clk,
	input       rst_n,
	input       uart_rx,
	output      uart_tx,

	// Configuration Flash
  output      FLASH_SPI_CS_N,
  output      FLASH_SPI_CLK,
  output      FLASH_SPI_SI,
  input       FLASH_SPI_SO
);

localparam [7:0] COOKIE = 8'hAF;

parameter        CLK_FRE  = 27;     // Mhz
parameter        UART_FRE = 115200; // Hz
reg[7:0]         tx_data;
reg              tx_data_valid;
wire             tx_data_ready;
wire[7:0]        rx_data;
wire             rx_data_valid;
wire             rx_data_ready;

assign rx_data_ready = 1'b1; // always can receive data


uart_rx#
(
	.CLK_FRE(CLK_FRE),
	.BAUD_RATE(UART_FRE)
) uart_rx_inst
(
	.clk                        (clk                      ),
	.rst_n                      (rst_n                    ),
	.rx_data                    (rx_data                  ),
	.rx_data_valid              (rx_data_valid            ),
	.rx_data_ready              (rx_data_ready            ),
	.rx_pin                     (uart_rx                  )
);

uart_tx#
(
	.CLK_FRE(CLK_FRE),
	.BAUD_RATE(UART_FRE)
) uart_tx_inst
(
	.clk                        (clk                      ),
	.rst_n                      (rst_n                    ),
	.tx_data                    (tx_data                  ),
	.tx_data_valid              (tx_data_valid            ),
	.tx_data_ready              (tx_data_ready            ),
	.tx_pin                     (uart_tx                  )
);

//---main-------------------------------------------------------------------------

localparam [2:0]
  idle       = 3'b000,
  read_bytes = 3'b001,
  execute    = 3'b010;

reg [2:0] state = idle;


localparam [7:0]
  get_cookie          = 8'd0,
  set_spi_ctrl_reg    = 8'd1,
  set_spi_data        = 8'd2,
  get_spi_data        = 8'd3;



reg [7:0] params[0:4];
reg [2:0] bytes_to_read;
reg [2:0] idx;
reg [7:0] cmd;

reg trigger_action = 1'b0;
reg spi_error = 1'b0;

always @(posedge clk) begin
  trigger_action <= 1'b0;

  if (rx_data_valid) begin
    case (state)
    idle:
      begin
        trigger_action <= 1'b0;
        cmd <= rx_data;
        state <= read_bytes;
        idx <= 3'b000;
        case (rx_data)
          get_cookie: begin
            trigger_action <= 1'b1;
            state <= idle;
          end
          set_spi_ctrl_reg: begin
            bytes_to_read <= 3'd1;
          end
          set_spi_data: begin
            bytes_to_read <= 3'd1;
          end
          get_spi_data: begin
            trigger_action <= 1'b1;
            state <= idle;
          end
          default:
            begin
              state <= idle;
              spi_error <= 1'b1;
            end
        endcase
      end
    read_bytes:
      begin
        params[idx] <= rx_data;
        idx <= idx + 3'b001;
        
        if (bytes_to_read == 3'd1)
          begin
            trigger_action <= 1'b1;
            state <= idle;
          end
        else
          bytes_to_read <= bytes_to_read - 3'd1;
      end
    default:
      state <= idle;
      endcase
  end
end


//----XFLASH---------------------------------------------------------------------

// SPI Flash control register
// bit7 is CS  (active high)
// bit6 is WPN (active low)
reg [7:0] spi_ctrl_reg = 8'h00;

always @(posedge clk)
begin
  if(trigger_action && cmd == set_spi_ctrl_reg)
    spi_ctrl_reg <= params[0];
end

// The SPI shift register is by design faster than the ESP can read and write.
// Therefore a status bit isn't necessary.  The ESP can read or write and then
// immediately read or write again on the next instruction.
reg [7:0] spi_shift_reg;
wire [7:0] spi_data_in = spi_shift_reg;
reg spi_sdo;
reg [7:0] spi_counter = 8'b0;

always @(posedge clk)
begin
   if(spi_counter[7])
   begin
      spi_counter <= spi_counter + 8'b1;
      if(spi_counter[2:0] == 3'b000)
      begin
         if(spi_counter[3] == 1'b0)
            spi_sdo <= spi_shift_reg[7];
         else
            spi_shift_reg <= {spi_shift_reg[6:0], FLASH_SPI_SO};
      end
   end
   else if(trigger_action && cmd == set_spi_data)
   begin
      spi_shift_reg <= params[0];
      spi_counter <= 8'b10000000;
   end
end

assign FLASH_SPI_CS_N = ~spi_ctrl_reg[7];
assign FLASH_SPI_CLK  = spi_counter[3];
assign FLASH_SPI_SI   = spi_sdo;


always @(posedge clk)
begin
  if (trigger_action)
    case (cmd)
      get_cookie: begin
		tx_data <= COOKIE;
		tx_data_valid <= 1'b1;
	  end
      get_spi_data: begin
		tx_data <= spi_data_in;
		tx_data_valid <= 1'b1;
	  end
    endcase
  else if (tx_data_ready) 
    tx_data_valid <= 1'b0;
end

endmodule