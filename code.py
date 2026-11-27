import argparse
import pathlib
import sys
import time

try:
    import serial
except ImportError:
    print("Missing dependency: pyserial. Install with: python -m pip install pyserial", file=sys.stderr)
    raise SystemExit(2)


def clean_gcode_line(line: str) -> str:
    return line.split(";", 1)[0].strip()


def wait_for_ready(port, timeout_seconds: float = 10.0) -> bool:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        message = raw.decode("ascii", errors="replace").strip()
        if message:
            print(f"< {message}")
        if message.startswith("READY BRAILLE_CNC_CONTROLLER"):
            return True
    return False


def wait_for_ack(port, timeout_seconds: float = 60.0) -> str:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        message = raw.decode("ascii", errors="replace").strip()
        if not message:
            continue
        print(f"< {message}")
        if message == "ok" or message.startswith("error:"):
            return message
    raise TimeoutError("controller did not acknowledge the command before timeout")


def main() -> int:
    parser = argparse.ArgumentParser(description="Stream a Braille CNC G-code file to an Arduino Uno.")
    parser.add_argument("--port", required=True, help="Serial port, for example COM3 or /dev/ttyACM0")
    parser.add_argument("--file", required=True, help="Path to generated .gcode file")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default: 115200)")
    parser.add_argument("--ack-timeout", type=float, default=60.0, help="Seconds to wait for each command")
    args = parser.parse_args()

    gcode_path = pathlib.Path(args.file)
    if not gcode_path.is_file():
        print(f"G-code file not found: {gcode_path}", file=sys.stderr)
        return 2

    commands = []
    with gcode_path.open("r", encoding="ascii") as handle:
        for line in handle:
            command = clean_gcode_line(line)
            if command:
                commands.append(command)

    if not commands:
        print("The G-code file contains no executable commands.", file=sys.stderr)
        return 2

    print(f"Port: {args.port} | Baud: {args.baud} | Commands: {len(commands)}")
    print("SAFETY CHECK: confirm the stylus is clear, the workpiece is secured, and the emergency stop can cut actuator power.")
    confirmation = input("Type RUN to start the job: ").strip()
    if confirmation != "RUN":
        print("Cancelled.")
        return 1

    try:
        # Opening a Uno serial port normally resets the board; allow it to boot.
        with serial.Serial(args.port, args.baud, timeout=0.25, write_timeout=3) as port:
            if not wait_for_ready(port):
                print("Controller READY message not received. Check the port, baud rate, and firmware.", file=sys.stderr)
                return 1

            for index, command in enumerate(commands, start=1):
                print(f"> [{index}/{len(commands)}] {command}")
                port.write((command + "\n").encode("ascii"))
                port.flush()
                ack = wait_for_ack(port, args.ack_timeout)
                if ack.startswith("error:"):
                    print(f"Stopped at command {index}: {ack}", file=sys.stderr)
                    return 1
                if ack != "ok":
                    print(f"Unexpected controller response: {ack}", file=sys.stderr)
                    return 1

        print("Job completed. The controller acknowledged all commands.")
        return 0
    except serial.SerialException as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
        return 1
    except TimeoutError as exc:
        print(f"Timeout: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
