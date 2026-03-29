# Build and Run

## Compile
```bash
gcc -O2 -Wall -Wextra -o sender sender.c -ldl -lm
gcc -O2 -Wall -Wextra -o receiver receiver.c -ldl -lm
gcc -O2 -Wall -Wextra -o threshold threshold.c -ldl -lm
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
./receiver -t 600 -b 200
```
**-t** means threshold, **-b** means bit duration (Default:200).

### Sender:
```bash
./sender -m "test123" -t 200
```
**-m** can be manually changed(Default:"HELLO WORLD/n"), default is **-t** means bit duration(Default:200).

## Expected Output

The receiver should print something like:
```bash
[SYNC]
[LEN=7]
test123[END]
```
