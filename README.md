# enelog

`enelog` is a lightweight command-line utility for measuring CPU power consumption using Intel's RAPL (Running Average Power Limit) interface. It is implemented in C and built with CMake.

## Features

- Measures CPU power usage via Intel RAPL
- Minimal codebase (single C source file)
- CMake-based build system

## Build Instructions

Make sure you have CMake (version 3.22 or higher) and a C compiler installed.

```bash
git clone https://github.com/your-username/enelog.git
cd enelog
cmake -B build
cmake --build build
```

After building, the executable will be available at `build/enelog`.

## Usage

```bash
./enelog [-i <interval>] [-t <timeout>] [-h]
```
- `-i <interval>`: Interval in seconds between power measurements (default: 1 second)
- `-t <timeout>`: Timeout in seconds after which the program exits (default: 10 seconds)

This command runs the program, which reports the CPU's power consumption using RAPL.

> **Note:** Accessing the RAPL interface typically requires root privileges. Run the binary with `sudo` if necessary.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Notes

- Works only on Intel processors that support RAPL
- For more information on RAPL, see the [Linux powercap documentation](https://www.kernel.org/doc/Documentation/power/powercap/powercap.txt)
