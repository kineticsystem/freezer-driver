# The Freezer Board

The drawings and the manufacturing files of the Freezer board, the circuit that Freezer Driver controls: an Arduino Nano 3.0 driving two 74HC595 shift registers, whose 16 outputs drive 16 optocouplers, four LTV847, wired to 8 output jacks and 1 input jack.

These are the files of the board as it was made, in July 2011. A later edit of the design, of August 2011, moved the parts of the PCB around without changing the circuit, and was never finished nor made; it is not kept here.

Fritzing 1.0 opens the design, made with Fritzing 0.5, and warns that some of its parts are obsolete; they still show and connect as they did.

## Files

| File | What it is |
|---|---|
| [`CircuitSchema.png`](CircuitSchema.png) | The circuit: the Nano, the two 74HC595, the four LTV847 and their resistors. Its pin labels are wrong, see below. |
| [`FreezerPCB.svg`](FreezerPCB.svg), [`FreezerPCB.png`](FreezerPCB.png) | The PCB, as made: the jacks OUT1 to OUT8 and IN1 along the top edge, the optocouplers, the resistors, the shift registers and the Nano. |
| [`gerber`](gerber) | The Gerber files the PCB was made from: the contour, the copper and the solder mask of both sides, the bottom silkscreen and the drill file. |
| [`fritzing/Freezer.fzz`](fritzing/Freezer.fzz) | The source of the design, in [Fritzing](https://fritzing.org): the breadboard, schematic and PCB views from which the PCB images and the Gerber files were exported, on 31 July 2011. The file to open to change the board. |
| [`CameraLogic.png`](CameraLogic.png) | How a camera's remote release works: its focus and shutter lines are pulled up, and closing one to ground triggers it. An optocoupler of the board closes it like the camera's own button. |
| [`StereoPlug25mm.png`](StereoPlug25mm.png) | The 2.5 mm stereo plug of a camera's remote release: tip, ring and sleeve. |
| [`ArduinoNano30.png`](ArduinoNano30.png) | The pins of the Arduino Nano 3.0. |
| [`ShiftRegister595.png`](ShiftRegister595.png) | The pins of the 74HC595. |
| [`LTV847.png`](LTV847.png) | The pins of the LTV847, four optocouplers in one package. |

## The Wiring

Every output jack is the same: a stereo jack fed by two optocouplers, i.e. two lines, each closed against the sleeve. For jack k, bit 2(k-1) of the 16 outputs closes the shutter line, the tip, and bit 2(k-1)+1 the focus line, the ring. OUT8, which the default sequences use for the flash, is wired like the others: a flash sync is a single line, so a sequence closes both lines of a flash jack together, which fires it whether its cable uses the tip or the ring, and does nothing more with a mono plug, whose sleeve the ring touches.

| Nano pin | Use |
|---|---|
| D2 | `MR` of the 74HC595, low active |
| D3 | `SH_CP` of the 74HC595 |
| D4 | `DS` of the 74HC595 |
| D5 | `OE` of the 74HC595, low active |
| D6 | `ST_CP` of the 74HC595 |
| D7 | IN1, the remote trigger, pulled down by R2 |
| D8 | a second input, which the original firmware reads; no jack on the board |

> [!IMPORTANT]
> The pin labels of `CircuitSchema.png` do not match the PCB: e.g. the circuit draws `DS` on D2. The circuit predates the PCB by six weeks, and the PCB, the original firmware of the board and the firmware of this project agree on the table above, which is the truth.
