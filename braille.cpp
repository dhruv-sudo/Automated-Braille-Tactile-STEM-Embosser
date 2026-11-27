#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Cell {
    // Bit 0 = dot 1, bit 1 = dot 2, ... bit 5 = dot 6.
    std::uint8_t mask = 0;
};

struct Options {
    std::string input;
    std::string output;
    double dotPitch = 2.5;
    double cellPitch = 6.0;
    double linePitch = 10.0;
    int pressAngle = 65;
    int dwellMs = 160;
    bool mirrorX = true;
};

static std::uint8_t dots(std::initializer_list<int> values) {
    std::uint8_t result = 0;
    for (int dot : values) {
        if (dot < 1 || dot > 6) throw std::runtime_error("Braille dot must be between 1 and 6");
        result |= static_cast<std::uint8_t>(1u << (dot - 1));
    }
    return result;
}

static const std::map<char, std::uint8_t>& letterMap() {
    static const std::map<char, std::uint8_t> table = {
        {'a', dots({1})},       {'b', dots({1,2})},     {'c', dots({1,4})},
        {'d', dots({1,4,5})},   {'e', dots({1,5})},     {'f', dots({1,2,4})},
        {'g', dots({1,2,4,5})},{'h', dots({1,2,5})},   {'i', dots({2,4})},
        {'j', dots({2,4,5})},   {'k', dots({1,3})},     {'l', dots({1,2,3})},
        {'m', dots({1,3,4})},   {'n', dots({1,3,4,5})},{'o', dots({1,3,5})},
        {'p', dots({1,2,3,4})},{'q', dots({1,2,3,4,5})},{'r', dots({1,2,3,5})},
        {'s', dots({2,3,4})},   {'t', dots({2,3,4,5})},{'u', dots({1,3,6})},
        {'v', dots({1,2,3,6})},{'w', dots({2,4,5,6})},{'x', dots({1,3,4,6})},
        {'y', dots({1,3,4,5,6})},{'z', dots({1,3,5,6})}
    };
    return table;
}

static const std::map<char, std::uint8_t>& punctuationMap() {
    // Basic punctuation subset. This is not a complete UEB or Nemeth table.
    static const std::map<char, std::uint8_t> table = {
        {',', dots({2})},       {';', dots({2,3})},     {':', dots({2,5})},
        {'.', dots({2,5,6})},   {'?', dots({2,3,6})},   {'!', dots({2,3,5})},
        {'\'', dots({3})},      {'-', dots({3,6})},     {'"', dots({5,6})}
    };
    return table;
}

static void appendCharacter(char ch, std::vector<Cell>& cells, bool& inNumber, std::size_t lineNo) {
    if (ch == ' ') {
        cells.push_back(Cell{0}); // One blank Braille cell is a word separator.
        inNumber = false;
        return;
    }

    const unsigned char uch = static_cast<unsigned char>(ch);
    if (uch > 127) {
        throw std::runtime_error("line " + std::to_string(lineNo) +
                                 ": non-ASCII input is not supported; use plain English text");
    }

    if (ch >= '0' && ch <= '9') {
        if (!inNumber) cells.push_back(Cell{dots({3,4,5,6})}); // Numeric indicator.
        const char letter = (ch == '0') ? 'j' : static_cast<char>('a' + (ch - '1'));
        cells.push_back(Cell{letterMap().at(letter)});
        inNumber = true;
        return;
    }

    if (std::isalpha(uch)) {
        // A grade-1 symbol indicator (dots 5-6) ends numeric interpretation
        // before a letter immediately following a numeric run.
        if (inNumber) cells.push_back(Cell{dots({5,6})});
        inNumber = false;

        const bool isUpper = std::isupper(uch) != 0;
        const char lower = static_cast<char>(std::tolower(uch));
        if (isUpper) cells.push_back(Cell{dots({6})}); // Capital indicator.
        cells.push_back(Cell{letterMap().at(lower)});
        return;
    }

    const auto punctuation = punctuationMap().find(ch);
    if (punctuation != punctuationMap().end()) {
        cells.push_back(Cell{punctuation->second});
        inNumber = false;
        return;
    }

    std::ostringstream message;
    message << "line " << lineNo << ": unsupported character '" << ch
            << "'. Supported: A-Z, a-z, 0-9, space, , ; : . ? ! ' - and \"";
    throw std::runtime_error(message.str());
}

static std::vector<std::vector<Cell>> encodeText(const std::vector<std::string>& lines) {
    std::vector<std::vector<Cell>> result;
    result.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::vector<Cell> row;
        bool inNumber = false;
        for (char ch : lines[i]) appendCharacter(ch, row, inNumber, i + 1);
        result.push_back(std::move(row));
    }
    return result;
}

static void printHelp(const char* program) {
    std::cout
        << "Braille CNC Studio - basic uncontracted English Braille G-code generator\n\n"
        << "Usage:\n  " << program << " --input text.txt --output job.gcode [options]\n\n"
        << "Options:\n"
        << "  --dot-pitch MM     Distance between dots in one cell (default 2.5)\n"
        << "  --cell-pitch MM    Horizontal pitch between cell origins (default 6.0)\n"
        << "  --line-pitch MM    Vertical pitch between text lines (default 10.0)\n"
        << "  --press-angle DEG  Servo press angle (default 65)\n"
        << "  --dwell-ms MS      Time to hold each dot (default 160)\n"
        << "  --no-mirror-x      Disable horizontal mirroring (mirroring is default)\n"
        << "  --help             Show this help\n";
}

static double parseDouble(const std::string& value, const std::string& option) {
    std::size_t used = 0;
    double parsed = 0.0;
    try { parsed = std::stod(value, &used); }
    catch (...) { throw std::runtime_error("invalid value for " + option + ": " + value); }
    if (used != value.size() || !std::isfinite(parsed))
        throw std::runtime_error("invalid value for " + option + ": " + value);
    return parsed;
}

static int parseInt(const std::string& value, const std::string& option) {
    std::size_t used = 0;
    int parsed = 0;
    try { parsed = std::stoi(value, &used); }
    catch (...) { throw std::runtime_error("invalid value for " + option + ": " + value); }
    if (used != value.size()) throw std::runtime_error("invalid value for " + option + ": " + value);
    return parsed;
}

static Options parseArgs(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto requireValue = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value after " + name);
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            printHelp(argv[0]);
            std::exit(0);
        } else if (arg == "--input") options.input = requireValue(arg);
        else if (arg == "--output") options.output = requireValue(arg);
        else if (arg == "--dot-pitch") options.dotPitch = parseDouble(requireValue(arg), arg);
        else if (arg == "--cell-pitch") options.cellPitch = parseDouble(requireValue(arg), arg);
        else if (arg == "--line-pitch") options.linePitch = parseDouble(requireValue(arg), arg);
        else if (arg == "--press-angle") options.pressAngle = parseInt(requireValue(arg), arg);
        else if (arg == "--dwell-ms") options.dwellMs = parseInt(requireValue(arg), arg);
        else if (arg == "--no-mirror-x") options.mirrorX = false;
        else throw std::runtime_error("unknown option: " + arg);
    }
    if (options.input.empty() || options.output.empty())
        throw std::runtime_error("both --input and --output are required (use --help)");
    if (options.dotPitch <= 0 || options.cellPitch <= 0 || options.linePitch <= 0)
        throw std::runtime_error("pitch values must be greater than zero");
    if (options.cellPitch <= options.dotPitch)
        throw std::runtime_error("cell pitch must be greater than dot pitch");
    if (options.pressAngle < 0 || options.pressAngle > 180)
        throw std::runtime_error("press angle must be between 0 and 180 degrees");
    if (options.dwellMs < 0 || options.dwellMs > 5000)
        throw std::runtime_error("dwell time must be between 0 and 5000 ms");
    return options;
}

static void writeGcode(const Options& options, const std::vector<std::vector<Cell>>& rows,
                       std::ostream& out) {
    out << "; Braille CNC Studio generated file\n"
        << "; Basic uncontracted English Braille subset; validate output with a Braille reader\n"
        << "; Coordinate units: mm. X mirroring: " << (options.mirrorX ? "ON" : "OFF") << "\n"
        << "G21\n"       // millimetres
        << "G90\n"       // absolute positioning
        << "M5\n"        // ensure stylus is released
        << "G92 X0 Y0\n"; // set current physical tool position as the job origin

    out << std::fixed << std::setprecision(3);
    std::size_t totalDots = 0;
    for (const auto& row : rows) {
        for (const auto& cell : row) {
            for (int bit = 0; bit < 6; ++bit) if (cell.mask & (1u << bit)) ++totalDots;
        }
    }

    std::size_t dotIndex = 0;
    for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const auto& row = rows[rowIndex];
        if (row.empty()) continue;
        const double rowWidth = (row.size() - 1) * options.cellPitch + options.dotPitch;
        const double yBase = rowIndex * options.linePitch;
        out << "; Text line " << (rowIndex + 1) << ", cells " << row.size() << "\n";

        for (std::size_t cellIndex = 0; cellIndex < row.size(); ++cellIndex) {
            const std::uint8_t mask = row[cellIndex].mask;
            for (int bit = 0; bit < 6; ++bit) {
                if ((mask & (1u << bit)) == 0) continue;
                const double xOffset = (bit >= 3) ? options.dotPitch : 0.0;
                const double yOffset = (bit % 3) * options.dotPitch;
                double x = cellIndex * options.cellPitch + xOffset;
                const double y = yBase + yOffset;
                if (options.mirrorX) x = rowWidth - x;

                out << "G0 X" << x << " Y" << y << "\n"
                    << "M3 S" << options.pressAngle << "\n"
                    << "G4 P" << options.dwellMs << "\n"
                    << "M5\n";
                ++dotIndex;
            }
        }
    }
    out << "M5\nM2\n";
    if (!out) throw std::runtime_error("failed while writing G-code output");
    std::cerr << "Generated " << dotIndex << " raised-dot operations across "
              << rows.size() << " input line(s).\n";
    (void)totalDots;
}

int main(int argc, char** argv) {
    try {
        const Options options = parseArgs(argc, argv);
        std::ifstream input(options.input, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open input file: " + options.input);

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
        if (lines.empty()) lines.emplace_back();

        const auto encoded = encodeText(lines);
        std::ofstream output(options.output, std::ios::binary);
        if (!output) throw std::runtime_error("cannot create output file: " + options.output);
        writeGcode(options, encoded, output);
        std::cout << "G-code saved to: " << options.output << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
