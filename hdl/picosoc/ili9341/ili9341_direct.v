
module ili9341_direct
(
  input            resetn,
  input            clk,
  input            iomem_valid,
  output reg       iomem_ready,
  input [3:0]      iomem_wstrb,
  input [31:0]     iomem_addr,
  input [31:0]     iomem_wdata,
  output [31:0]    iomem_rdata,
  output reg       nreset,
  output reg       cmd_data, // 1 => Data, 0 => Command
  output reg       ncs, // Chip select (low enable)
  output reg       write_edge, // Write signal on rising edge
  output           read_edge, // Read signal on rising edge
  output           backlight,
  output reg [7:0] dout
);

  reg [1:0] state = 0;

  assign backlight = 1;
  assign read_edge = 0;

  always @(posedge clk) begin
    iomem_ready <= 0;
    if (!resetn) begin
      state <= 0;
      ncs <= 1;
      cmd_data <= 0;
      nreset <= 1;
      write_edge <= 0;
    end else if (iomem_valid && !iomem_ready) begin
      if (iomem_wstrb) begin
        iomem_ready <= 1;
        if (iomem_addr[7:0] == 'h04) ncs <= iomem_wdata;
        else if (iomem_addr[7:0] == 'h08) cmd_data <= iomem_wdata;
        else if (iomem_addr[7:0] == 'h0c) nreset <= iomem_wdata;
        else if (iomem_addr[7:0] == 'h00) begin
          case (state)
            0 : begin
              write_edge <= 0;
              dout <= iomem_wdata[7:0];
              state <= 1;
              iomem_ready <= 0;
            end
            1 : begin
               write_edge <= 1;
               state <= 2;
               iomem_ready <= 0;
            end
            2 : begin
               write_edge <= 0;
               iomem_ready <= 1;
               state <= 0;
            end
          endcase
        end
      end
    end
  end

endmodule
