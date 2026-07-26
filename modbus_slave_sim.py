"""
Modbus TCP Slave Simulator — 配合 EonTest ModbusMaster 插件测试使用
用法: python modbus_slave_sim.py

启动后在 TCP:502 监听，默认填充 Holding Registers 0x0000-0x000F:
  0x0000=1000  0x0001=2000  0x0002=3000  0x0003=4000
  0x0004=5000  0x0005=6000  0x0006=7000  0x0007=8000
  ...
"""

import struct
import socket
import threading
import sys

# ============================================================
# Holding Registers 初始值
# ============================================================
holding_registers = [i * 1000 for i in range(128)]  # 0x0000-0x007F


def handle_client(conn, addr):
    print(f"[+] Client connected: {addr}")
    try:
        while True:
            data = conn.recv(1024)
            if not data:
                break

            # Modbus TCP 帧: MBAP(7) + PDU
            if len(data) < 7:
                continue

            tid = struct.unpack(">H", data[0:2])[0]      # Transaction ID
            proto = struct.unpack(">H", data[2:4])[0]     # Protocol (0=Modbus)
            length = struct.unpack(">H", data[4:6])[0]    # Length
            unit_id = data[6]                             # Unit ID
            func = data[7]                                # Function code
            pdu = data[7:]                                # PDU

            print(f"    ← RX: TID={tid} Unit={unit_id} Func=0x{func:02X} PDU={pdu.hex()}")

            if func == 3:  # Read Holding Registers
                addr_start = struct.unpack(">H", pdu[1:3])[0]
                reg_count = struct.unpack(">H", pdu[3:5])[0]
                print(f"      Read HR: addr=0x{addr_start:04X} count={reg_count}")

                # Build response
                byte_count = reg_count * 2
                resp = bytearray()
                resp.append(func)
                resp.append(byte_count)
                for i in range(reg_count):
                    idx = addr_start + i
                    val = holding_registers[idx] if idx < len(holding_registers) else 0
                    resp.extend(struct.pack(">H", val))

                # Wrap in MBAP
                mbap = struct.pack(">HHHB", tid, 0, len(resp) + 1, unit_id)
                conn.send(mbap + bytes(resp))
                print(f"      → TX: {[holding_registers[addr_start + i] if (addr_start + i) < len(holding_registers) else 0 for i in range(reg_count)]}")

            elif func == 6:  # Write Single Register
                addr_start = struct.unpack(">H", pdu[1:3])[0]
                value = struct.unpack(">H", pdu[3:5])[0]
                if addr_start < len(holding_registers):
                    holding_registers[addr_start] = value
                print(f"      Write HR: addr=0x{addr_start:04X} value={value}")

                # Echo response (标准响应 = 请求原样返回)
                mbap = struct.pack(">HHHB", tid, 0, len(pdu) + 1, unit_id)
                conn.send(mbap + pdu)

            elif func == 16:  # Write Multiple Registers
                addr_start = struct.unpack(">H", pdu[1:3])[0]
                reg_count = struct.unpack(">H", pdu[3:5])[0]
                byte_count = pdu[5]
                print(f"      Write Multiple HR: addr=0x{addr_start:04X} count={reg_count}")

                for i in range(reg_count):
                    val = struct.unpack(">H", pdu[6 + i*2:8 + i*2])[0]
                    idx = addr_start + i
                    if idx < len(holding_registers):
                        holding_registers[idx] = val

                # 返回: func + addr + count
                resp = bytearray()
                resp.append(func)
                resp.extend(struct.pack(">H", addr_start))
                resp.extend(struct.pack(">H", reg_count))
                mbap = struct.pack(">HHHB", tid, 0, len(resp) + 1, unit_id)
                conn.send(mbap + bytes(resp))

            else:
                # 不支持的功能码 → 异常响应
                print(f"      [!] Unsupported function: 0x{func:02X}")
                resp = bytearray()
                resp.append(func | 0x80)
                resp.append(0x01)  # Illegal function
                mbap = struct.pack(">HHHB", tid, 0, len(resp) + 1, unit_id)
                conn.send(mbap + bytes(resp))

    except ConnectionResetError:
        pass
    except Exception as e:
        print(f"    [!] Error: {e}")
    finally:
        conn.close()
        print(f"[-] Client disconnected: {addr}")


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "0.0.0.0"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 1502  # 非特权端口

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((host, port))
    server.listen(5)
    print(f"\n{'='*50}")
    print(f"  Modbus TCP Slave Simulator")
    print(f"  Listening on {host}:{port}")
    print(f"\n  Holding Registers 0x0000-0x000F:")
    print(f"    " + " ".join(f"0x{i:04X}={holding_registers[i]}" for i in range(16)))
    print(f"{'='*50}\n")

    try:
        while True:
            conn, addr = server.accept()
            threading.Thread(target=handle_client, args=(conn, addr), daemon=True).start()
    except KeyboardInterrupt:
        print("\nShutting down...")
        server.close()


if __name__ == "__main__":
    main()
