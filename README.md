# Build and Run

## Compile
```bash
make
```

## Threshold Tunning

Run:
```bash
./threshold
```
Choose a threshold between the average flushed and non-flushed timings.

## Test the Channel

Open two terminals.

### Receiver:
```bash
./receiver -t 600 -d 200
```
**-t** means threshold, **-d** means bit duration (Default:200).

### Sender:
```bash
./sender -m "test123" -d 200
```
**-m** can be manually changed(Default:"HELLO WORLD/n"), and **-d** means bit duration(Default:200).

## Expected Output

The receiver should print something like:
```bash
[SYNC]
[LEN=7]
test123[END]
```
