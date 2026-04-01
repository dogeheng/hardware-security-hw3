# Build and Run

## Compile
```bash
make
```

## Threshold Tuning

Run:
```bash
./threshold
```
Choose a threshold between the average flushed and non-flushed timings.

## Test the Packetized Channel

Open two terminals.

### Receiver
```bash
./receiver -t 600 -d 5
```
`-t` is the timing threshold in cycles and `-d` is the bit duration in milliseconds.

### Sender: message from the command line
```bash
./sender -m "Packetized covert channels survive duplicates and CRC checks." -d 5 -p 16
```

### Sender: message from a file
```bash
./sender -f report_assets/test_payload.txt -d 5 -p 16
```

Sender flags:
- `-m` sends a message from the command line.
- `-f` reads the message from a file.
- `-d` sets the bit duration in milliseconds.
- `-p` sets the payload bytes per packet.
- `-r` adds immediate packet repetition inside one transmission cycle. The default is `1` because the full packet stream is already repeated forever.

## Wire Format

Each packet is transmitted with this structure:
```text
[SYNC=0xA5 0x5A][session_id][packet_index][packet_count][payload_len][payload][crc16]
```

The receiver validates each packet with CRC-16, stores packets by sequence number, and reassembles the full message once all packets for the current session have arrived.

## Example Output

The receiver should print something like:
```text
[Receiver] Session 0x634c packet_count=4
[Receiver] Packet 4/4 stored len=13 progress=1/4
[Receiver] Packet 1/4 stored len=16 progress=2/4
[Receiver] Packet 2/4 stored len=16 progress=3/4
[Receiver] Packet 3/4 stored len=16 progress=4/4
[MESSAGE COMPLETE session=0x634c packets=4 len=61]
Packetized covert channels survive duplicates and CRC checks.
[END MESSAGE]
```
