#include "camera/gige/genicam.h"

#include <QByteArray>
#include <QString>
#include <QXmlStreamReader>

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <stdlib.h>
#include <cstdio>
#include <cstring>
#include <functional>

namespace calib
{

namespace
{

constexpr int kMaxDepth = 64;

std::string trim(const std::string& text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while(begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
    {
        ++begin;
    }
    while(end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool parseUnsigned(const std::string& digits, int base, std::uint64_t* value)
{
    if(digits.empty())
    {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    *value = strtoull(digits.c_str(), &end, base);
    return errno == 0 && end == digits.c_str() + digits.size();
}

bool parseNumber(const std::string& text, std::int64_t* value)
{
    const std::string t = trim(text);
    const bool negative = !t.empty() && t[0] == '-';
    const std::string digits = negative ? t.substr(1) : t;
    const bool hex = digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X');
    std::uint64_t magnitude = 0;
    if(!parseUnsigned(hex ? digits.substr(2) : digits, hex ? 16 : 10, &magnitude))
    {
        return false;
    }
    *value = negative ? -static_cast<std::int64_t>(magnitude) : static_cast<std::int64_t>(magnitude);
    return true;
}


bool parseReal(const std::string& text, double* value)
{
    const std::string t = trim(text);
    if(t.empty())
    {
        return false;
    }
    std::int64_t integer = 0;
    if(parseNumber(t, &integer))
    {
        *value = static_cast<double>(integer);
        return true;
    }
    char* end = nullptr;
    errno = 0;
    *value = strtod(t.c_str(), &end);
    return errno == 0 && end == t.c_str() + t.size();
}


// SwissKnife / Converter formulas: C-like operators plus GenICam's "="
// (equal), "<>" (not equal), "**" (power) and functions such as ABS, SQRT,
// ROUND. Integer mode (IntSwissKnife) truncates divisions.
class FormulaParser
{
private:
    const std::string& text;
    std::size_t position = 0;
    std::function<bool(const std::string&, double*)> lookup;
    bool integer;
    bool ok = true;

public:
    FormulaParser(const std::string& text, std::function<bool(const std::string&, double*)> lookup, bool integer)
        : text(text),
          lookup(std::move(lookup)),
          integer(integer)
    {
    }

    bool parse(double* value)
    {
        *value = ternary();
        skipSpace();
        return ok && position == text.size();
    }

private:
    static std::int64_t toInt(double value)
    {
        return static_cast<std::int64_t>(std::llround(value));
    }

    void skipSpace(void)
    {
        while(position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
        {
            ++position;
        }
    }

    bool accept(const char* token)
    {
        skipSpace();
        const std::size_t length = std::strlen(token);
        if(text.compare(position, length, token) == 0)
        {
            position += length;
            return true;
        }
        return false;
    }

    bool acceptUnless(const char* token, const char* longer1, const char* longer2 = nullptr)
    {
        skipSpace();
        if(text.compare(position, std::strlen(longer1), longer1) == 0
           || (longer2 && text.compare(position, std::strlen(longer2), longer2) == 0))
        {
            return false;
        }
        return accept(token);
    }

    double ternary(void)
    {
        const double condition = logicalOr();
        if(accept("?"))
        {
            const double whenTrue = ternary();
            if(!accept(":"))
            {
                ok = false;
                return 0;
            }
            const double whenFalse = ternary();
            return condition != 0.0 ? whenTrue : whenFalse;
        }
        return condition;
    }

    double logicalOr(void)
    {
        double value = logicalAnd();
        while(accept("||"))
        {
            const double right = logicalAnd();
            value = (value != 0.0 || right != 0.0) ? 1 : 0;
        }
        return value;
    }

    double logicalAnd(void)
    {
        double value = bitOr();
        while(accept("&&"))
        {
            const double right = bitOr();
            value = (value != 0.0 && right != 0.0) ? 1 : 0;
        }
        return value;
    }

    double bitOr(void)
    {
        double value = bitXor();
        while(acceptUnless("|", "||"))
        {
            value = static_cast<double>(toInt(value) | toInt(bitXor()));
        }
        return value;
    }

    double bitXor(void)
    {
        double value = bitAnd();
        while(accept("^"))
        {
            value = static_cast<double>(toInt(value) ^ toInt(bitAnd()));
        }
        return value;
    }

    double bitAnd(void)
    {
        double value = equality();
        while(acceptUnless("&", "&&"))
        {
            value = static_cast<double>(toInt(value) & toInt(equality()));
        }
        return value;
    }

    double equality(void)
    {
        double value = comparison();
        while(true)
        {
            if(accept("<>") || accept("!="))
            {
                value = (value != comparison()) ? 1 : 0;
            }
            else if(accept("==") || accept("="))
            {
                value = (value == comparison()) ? 1 : 0;
            }
            else
            {
                return value;
            }
        }
    }

    double comparison(void)
    {
        double value = shift();
        while(true)
        {
            if(accept("<="))
            {
                value = (value <= shift()) ? 1 : 0;
            }
            else if(accept(">="))
            {
                value = (value >= shift()) ? 1 : 0;
            }
            else if(acceptUnless("<", "<<", "<>"))
            {
                value = (value < shift()) ? 1 : 0;
            }
            else if(acceptUnless(">", ">>"))
            {
                value = (value > shift()) ? 1 : 0;
            }
            else
            {
                return value;
            }
        }
    }

    double shift(void)
    {
        double value = additive();
        while(true)
        {
            if(accept("<<"))
            {
                value = static_cast<double>(
                    static_cast<std::int64_t>(static_cast<std::uint64_t>(toInt(value)) << (toInt(additive()) & 63)));
            }
            else if(accept(">>"))
            {
                value = static_cast<double>(
                    static_cast<std::int64_t>(static_cast<std::uint64_t>(toInt(value)) >> (toInt(additive()) & 63)));
            }
            else
            {
                return value;
            }
        }
    }

    double additive(void)
    {
        double value = multiplicative();
        while(true)
        {
            if(accept("+"))
            {
                value += multiplicative();
            }
            else if(accept("-"))
            {
                value -= multiplicative();
            }
            else
            {
                return value;
            }
        }
    }

    double multiplicative(void)
    {
        double value = power();
        while(true)
        {
            if(acceptUnless("*", "**"))
            {
                value *= power();
            }
            else if(accept("/"))
            {
                const double divisor = power();
                if(divisor == 0.0)
                {
                    ok = false;
                    return 0;
                }
                value = integer ? static_cast<double>(toInt(value) / toInt(divisor)) : value / divisor;
            }
            else if(accept("%"))
            {
                const std::int64_t divisor = toInt(power());
                if(divisor == 0)
                {
                    ok = false;
                    return 0;
                }
                value = static_cast<double>(toInt(value) % divisor);
            }
            else
            {
                return value;
            }
        }
    }

    double power(void)
    {
        const double base = unary();
        if(accept("**"))
        {
            return std::pow(base, power());
        }
        return base;
    }

    double unary(void)
    {
        if(accept("-"))
        {
            return -unary();
        }
        if(accept("+"))
        {
            return unary();
        }
        if(accept("~"))
        {
            return static_cast<double>(~toInt(unary()));
        }
        if(acceptUnless("!", "!="))
        {
            return unary() != 0.0 ? 0 : 1;
        }
        return primary();
    }

    double function(const std::string& name)
    {
        std::vector<double> arguments;
        if(!accept(")"))
        {
            do
            {
                arguments.push_back(ternary());
            }
            while(accept(","));
            if(!accept(")"))
            {
                ok = false;
                return 0;
            }
        }
        const double x = arguments.empty() ? 0.0 : arguments.front();
        if(name == "ABS")
        {
            return std::fabs(x);
        }
        if(name == "SQRT")
        {
            return std::sqrt(x);
        }
        if(name == "TRUNC")
        {
            return std::trunc(x);
        }
        if(name == "FLOOR")
        {
            return std::floor(x);
        }
        if(name == "CEIL")
        {
            return std::ceil(x);
        }
        if(name == "EXP")
        {
            return std::exp(x);
        }
        if(name == "LN")
        {
            return std::log(x);
        }
        if(name == "LG")
        {
            return std::log10(x);
        }
        if(name == "SGN")
        {
            return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0);
        }
        if(name == "NEG")
        {
            return -x;
        }
        if(name == "SIN")
        {
            return std::sin(x);
        }
        if(name == "COS")
        {
            return std::cos(x);
        }
        if(name == "TAN")
        {
            return std::tan(x);
        }
        if(name == "ATAN")
        {
            return std::atan(x);
        }
        if(name == "ROUND")
        {
            const double scale = std::pow(10.0, arguments.size() > 1 ? arguments[1] : 0.0);
            return std::round(x * scale) / scale;
        }
        ok = false;
        return 0;
    }

    double primary(void)
    {
        skipSpace();
        if(accept("("))
        {
            const double value = ternary();
            if(!accept(")"))
            {
                ok = false;
            }
            return value;
        }

        const std::size_t start = position;
        if(position < text.size()
           && (std::isdigit(static_cast<unsigned char>(text[position])) || text[position] == '.'))
        {
            // Hex (0x1F), decimal (12) or real (1.5e-3) literal.
            while(position < text.size()
                  && (std::isalnum(static_cast<unsigned char>(text[position])) || text[position] == '.'
                      || ((text[position] == '-' || text[position] == '+') && position > start
                          && (text[position - 1] == 'e' || text[position - 1] == 'E')
                          && text.compare(start, 2, "0x") != 0)))
            {
                ++position;
            }
            double value = 0;
            if(!parseReal(text.substr(start, position - start), &value))
            {
                ok = false;
            }
            return value;
        }

        while(position < text.size()
              && (std::isalnum(static_cast<unsigned char>(text[position])) || text[position] == '_'
                  || text[position] == '.'))
        {
            ++position;
        }
        if(position == start)
        {
            ok = false;
            return 0;
        }
        const std::string name = text.substr(start, position - start);
        if(accept("("))
        {
            return function(name);
        }
        if(name == "PI")
        {
            return 3.14159265358979323846;
        }
        if(name == "E")
        {
            return 2.71828182845904523536;
        }
        double value = 0;
        if(!lookup(name, &value))
        {
            ok = false;
        }
        return value;
    }
};


bool readUint16Le(const std::string& data, std::size_t offset, std::uint32_t* value)
{
    if(offset + 2 > data.size())
    {
        return false;
    }
    const auto* p = reinterpret_cast<const unsigned char*>(data.data() + offset);
    *value = p[0] | (p[1] << 8);
    return true;
}

bool readUint32Le(const std::string& data, std::size_t offset, std::uint32_t* value)
{
    if(offset + 4 > data.size())
    {
        return false;
    }
    const auto* p = reinterpret_cast<const unsigned char*>(data.data() + offset);
    *value = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    return true;
}

bool inflateRaw(const char* data, std::size_t size, std::size_t expectedSize, std::string* out)
{
    out->assign(expectedSize, '\0');
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if(inflateInit2(&stream, -MAX_WBITS) != Z_OK)
    {
        return false;
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
    stream.avail_in = static_cast<uInt>(size);
    stream.next_out = reinterpret_cast<Bytef*>(&(*out)[0]);
    stream.avail_out = static_cast<uInt>(expectedSize);
    const int result = inflate(&stream, Z_FINISH);
    const std::size_t produced = stream.total_out;
    inflateEnd(&stream);
    out->resize(produced);
    return result == Z_STREAM_END;
}

} // namespace


bool GenICamNodeMap::load(const std::string& xml, std::string* error)
{
    nodes.clear();
    static const char* const kTypes[] = {"Integer",     "IntReg",    "MaskedIntReg",  "StructReg",  "Boolean",
                                         "Enumeration", "Command",   "IntSwissKnife", "SwissKnife", "Float",
                                         "FloatReg",    "Converter", "IntConverter",  "String",     "StringReg"};

    QXmlStreamReader reader(QByteArray::fromStdString(xml));
    while(!reader.atEnd())
    {
        reader.readNext();
        if(!reader.isStartElement())
        {
            continue;
        }
        const std::string type = reader.name().toString().toStdString();
        const std::string name = reader.attributes().value(QStringLiteral("Name")).toString().toStdString();
        if((name.empty() && type != "StructReg")
           || std::none_of(std::begin(kTypes), std::end(kTypes), [&](const char* t) { return type == t; }))
        {
            continue;
        }

        Node node;
        node.type = type;
        while(reader.readNextStartElement())
        {
            const std::string child = reader.name().toString().toStdString();
            const QXmlStreamAttributes attributes = reader.attributes();

            if(child == "EnumEntry")
            {
                const std::string entryName = attributes.value(QStringLiteral("Name")).toString().toStdString();
                std::int64_t value = 0;
                bool hasValue = false;
                while(reader.readNextStartElement())
                {
                    if(reader.name() == QLatin1String("Value"))
                    {
                        hasValue = parseNumber(reader.readElementText().toStdString(), &value);
                    }
                    else
                    {
                        reader.skipCurrentElement();
                    }
                }
                if(hasValue)
                {
                    node.entries.emplace_back(entryName, value);
                }
            }
            else if(child == "StructEntry")
            {
                Node entry;
                entry.type = "MaskedIntReg";
                for(const char* inherited : {"Address", "pAddress", "Length", "pPort", "Endianess", "Sign"})
                {
                    const auto it = node.fields.find(inherited);
                    if(it != node.fields.end())
                    {
                        entry.fields[inherited] = it->second;
                    }
                }
                entry.indexes = node.indexes;
                const std::string entryName = attributes.value(QStringLiteral("Name")).toString().toStdString();
                while(reader.readNextStartElement())
                {
                    const std::string field = reader.name().toString().toStdString();
                    entry.fields[field] = {
                        trim(reader.readElementText(QXmlStreamReader::SkipChildElements).toStdString())};
                }
                if(!entryName.empty())
                {
                    nodes[entryName] = std::move(entry);
                }
            }
            else if(child == "pVariable")
            {
                const std::string variable = attributes.value(QStringLiteral("Name")).toString().toStdString();
                node.variables[variable] = trim(reader.readElementText().toStdString());
            }
            else if(child == "Constant" || child == "Expression")
            {
                const std::string variable = attributes.value(QStringLiteral("Name")).toString().toStdString();
                node.constants[variable] = trim(reader.readElementText().toStdString());
            }
            else if(child == "pIndex")
            {
                std::string offset = attributes.value(QStringLiteral("Offset")).toString().toStdString();
                if(offset.empty())
                {
                    offset = "@" + attributes.value(QStringLiteral("pOffset")).toString().toStdString();
                }
                node.indexes.emplace_back(trim(reader.readElementText().toStdString()), offset);
            }
            else
            {
                node.fields[child].push_back(
                    trim(reader.readElementText(QXmlStreamReader::SkipChildElements).toStdString()));
            }
        }
        if(!name.empty())
        {
            nodes[name] = std::move(node);
        }
    }

    if(reader.hasError())
    {
        if(error)
        {
            *error = "GenICam XML: " + reader.errorString().toStdString() + " at line "
                + std::to_string(reader.lineNumber());
        }
        nodes.clear();
        return false;
    }
    if(nodes.empty())
    {
        if(error)
        {
            *error = "GenICam XML contains no features";
        }
        return false;
    }
    return true;
}

void GenICamNodeMap::setPort(IRegisterPort* port)
{
    this->port = port;
}

bool GenICamNodeMap::has(const std::string& name) const
{
    return nodes.count(name) != 0;
}

bool GenICamNodeMap::getInteger(const std::string& name, std::int64_t* value)
{
    depth = 0;
    return evaluate(name, value);
}

bool GenICamNodeMap::setInteger(const std::string& name, std::int64_t value)
{
    depth = 0;
    return assign(name, value);
}

bool GenICamNodeMap::execute(const std::string& name)
{
    depth = 0;
    const Node* node = find(name);
    if(!node)
    {
        return false;
    }
    if(node->type != "Command")
    {
        return fail(name + " is not a command");
    }

    std::int64_t commandValue = 1;
    const auto value = node->fields.find("CommandValue");
    const auto pointer = node->fields.find("pCommandValue");
    if(value != node->fields.end())
    {
        parseNumber(value->second.front(), &commandValue);
    }
    else if(pointer != node->fields.end() && !evaluate(pointer->second.front(), &commandValue))
    {
        return false;
    }

    const auto target = node->fields.find("pValue");
    if(target == node->fields.end())
    {
        return fail(name + " has no pValue");
    }
    return assign(target->second.front(), commandValue);
}

bool GenICamNodeMap::getEnumeration(const std::string& name, std::string* entry)
{
    depth = 0;
    const Node* node = find(name);
    std::int64_t value = 0;
    if(!node || !evaluate(name, &value))
    {
        return false;
    }
    for(const auto& [entryName, entryValue] : node->entries)
    {
        if(entryValue == value)
        {
            *entry = entryName;
            return true;
        }
    }
    *entry = std::to_string(value);
    return true;
}

bool GenICamNodeMap::setEnumeration(const std::string& name, const std::string& entry)
{
    depth = 0;
    const Node* node = find(name);
    if(!node)
    {
        return false;
    }
    for(const auto& [entryName, entryValue] : node->entries)
    {
        if(entryName == entry)
        {
            return assign(name, entryValue);
        }
    }
    return fail(name + " has no entry " + entry);
}

const GenICamNodeMap::Node* GenICamNodeMap::find(const std::string& name)
{
    const auto it = nodes.find(name);
    if(it == nodes.end())
    {
        fail("no feature " + name);
        return nullptr;
    }
    return &it->second;
}

bool GenICamNodeMap::evaluate(const std::string& name, std::int64_t* value)
{
    if(++depth > kMaxDepth)
    {
        return fail("feature references are nested too deeply at " + name);
    }
    struct DepthGuard
    {
        int& depth;
        ~DepthGuard(void)
        {
            --depth;
        }
    } guard{depth};

    const Node* node = find(name);
    if(!node)
    {
        return false;
    }
    const auto field = [node](const char* key) -> const std::string*
    {
        const auto it = node->fields.find(key);
        return it == node->fields.end() || it->second.empty() ? nullptr : &it->second.front();
    };

    if(node->type == "IntReg" || node->type == "MaskedIntReg")
    {
        return readRegister(*node, value);
    }
    if(node->type == "IntSwissKnife" || node->type == "SwissKnife" || node->type == "Float" || node->type == "FloatReg"
       || node->type == "Converter" || node->type == "IntConverter")
    {
        double number = 0;
        if(!evaluateNumber(name, &number))
        {
            return false;
        }
        *value = static_cast<std::int64_t>(std::llround(number));
        return true;
    }
    if(node->type == "Integer" || node->type == "Enumeration" || node->type == "Command" || node->type == "Boolean")
    {
        std::int64_t raw = 0;
        if(const std::string* pValue = field("pValue"))
        {
            if(!evaluate(*pValue, &raw))
            {
                return false;
            }
        }
        else if(const std::string* constant = field("Value"))
        {
            if(!parseNumber(*constant, &raw))
            {
                return fail(name + " has an invalid value");
            }
        }
        else
        {
            return fail(name + " has no value");
        }

        if(node->type == "Boolean")
        {
            std::int64_t on = 1;
            if(const std::string* onValue = field("OnValue"))
            {
                parseNumber(*onValue, &on);
            }
            raw = raw == on ? 1 : 0;
        }
        *value = raw;
        return true;
    }
    return fail(name + " is a " + node->type + ", which CalibStudio cannot read yet");
}

bool GenICamNodeMap::assign(const std::string& name, std::int64_t value)
{
    if(++depth > kMaxDepth)
    {
        return fail("feature references are nested too deeply at " + name);
    }
    struct DepthGuard
    {
        int& depth;
        ~DepthGuard(void)
        {
            --depth;
        }
    } guard{depth};

    const auto it = nodes.find(name);
    if(it == nodes.end())
    {
        return fail("no feature " + name);
    }
    Node& node = it->second;

    if(node.type == "IntReg" || node.type == "MaskedIntReg")
    {
        return writeRegister(node, value);
    }
    if(node.type == "Float" || node.type == "FloatReg" || node.type == "Converter" || node.type == "IntConverter")
    {
        return assignNumber(name, static_cast<double>(value));
    }
    if(node.type == "Integer" || node.type == "Enumeration" || node.type == "Boolean")
    {
        if(node.type == "Boolean")
        {
            std::int64_t on = 1;
            std::int64_t off = 0;
            if(node.fields.count("OnValue"))
            {
                parseNumber(node.fields["OnValue"].front(), &on);
            }
            if(node.fields.count("OffValue"))
            {
                parseNumber(node.fields["OffValue"].front(), &off);
            }
            value = value ? on : off;
        }
        const auto pValue = node.fields.find("pValue");
        if(pValue != node.fields.end() && !pValue->second.empty())
        {
            return assign(pValue->second.front(), value);
        }
        node.fields["Value"] = {std::to_string(value)};
        return true;
    }
    return fail(name + " is a " + node.type + ", which CalibStudio cannot write yet");
}

bool GenICamNodeMap::evaluateFormula(const Node& node, const std::string& formula,
                                     const std::map<std::string, double>& extra, bool integer, double* value)
{
    const auto lookup = [this, &node, &extra, integer](const std::string& variable, double* result) -> bool
    {
        const auto given = extra.find(variable);
        if(given != extra.end())
        {
            *result = given->second;
            return true;
        }
        const auto reference = node.variables.find(variable);
        if(reference != node.variables.end())
        {
            return evaluateNumber(reference->second, result);
        }
        const auto constant = node.constants.find(variable);
        if(constant != node.constants.end())
        {
            return parseReal(constant->second, result)
                || evaluateFormula(node, constant->second, extra, integer, result);
        }
        return fail("unknown variable " + variable + " in formula");
    };

    FormulaParser parser(formula, lookup, integer);
    if(!parser.parse(value))
    {
        return fail(lastError.empty() ? "cannot evaluate formula " + formula : lastError);
    }
    return true;
}

bool GenICamNodeMap::getNumber(const std::string& name, double* value)
{
    depth = 0;
    return evaluateNumber(name, value);
}

bool GenICamNodeMap::setNumber(const std::string& name, double value)
{
    depth = 0;
    return assignNumber(name, value);
}

bool GenICamNodeMap::getRange(const std::string& name, double* minimum, double* maximum)
{
    depth = 0;
    const Node* node = find(name);
    if(!node)
    {
        return false;
    }
    const auto limit = [this, node](const char* fixed, const char* pointer, double* result)
    {
        const auto constant = node->fields.find(fixed);
        if(constant != node->fields.end() && parseReal(constant->second.front(), result))
        {
            return true;
        }
        const auto reference = node->fields.find(pointer);
        return reference != node->fields.end() && evaluateNumber(reference->second.front(), result);
    };
    return limit("Min", "pMin", minimum) && limit("Max", "pMax", maximum);
}

bool GenICamNodeMap::isFloat(const std::string& name) const
{
    const auto it = nodes.find(name);
    if(it == nodes.end())
    {
        return false;
    }
    const std::string& type = it->second.type;
    return type == "Float" || type == "FloatReg" || type == "Converter" || type == "SwissKnife";
}

std::vector<std::string> GenICamNodeMap::enumerationEntries(const std::string& name) const
{
    std::vector<std::string> names;
    const auto it = nodes.find(name);
    if(it != nodes.end())
    {
        for(const auto& entry : it->second.entries)
        {
            names.push_back(entry.first);
        }
    }
    return names;
}

bool GenICamNodeMap::evaluateNumber(const std::string& name, double* value)
{
    if(++depth > kMaxDepth)
    {
        return fail("feature references are nested too deeply at " + name);
    }
    struct DepthGuard
    {
        int& depth;
        ~DepthGuard(void)
        {
            --depth;
        }
    } guard{depth};

    const Node* node = find(name);
    if(!node)
    {
        return false;
    }
    const auto field = [node](const char* key) -> const std::string*
    {
        const auto it = node->fields.find(key);
        return it == node->fields.end() || it->second.empty() ? nullptr : &it->second.front();
    };

    if(node->type == "FloatReg")
    {
        return readFloatRegister(*node, value);
    }
    if(node->type == "Float")
    {
        if(const std::string* pValue = field("pValue"))
        {
            return evaluateNumber(*pValue, value);
        }
        if(const std::string* constant = field("Value"))
        {
            return parseReal(*constant, value) || fail(name + " has an invalid value");
        }
        return fail(name + " has no value");
    }
    if(node->type == "Converter" || node->type == "IntConverter")
    {
        const std::string* pValue = field("pValue");
        const std::string* formula = field("FormulaFrom");
        double raw = 0;
        if(!pValue || !formula || !evaluateNumber(*pValue, &raw))
        {
            return pValue && formula ? false : fail(name + " is incomplete");
        }
        return evaluateFormula(*node, *formula,
                               {
                                   {"TO", raw}
        },
                               node->type == "IntConverter", value);
    }
    if(node->type == "SwissKnife" || node->type == "IntSwissKnife")
    {
        const std::string* formula = field("Formula");
        return formula ? evaluateFormula(*node, *formula, {}, node->type == "IntSwissKnife", value)
                       : fail(name + " has no formula");
    }

    std::int64_t integer = 0;
    if(!evaluate(name, &integer))
    {
        return false;
    }
    *value = static_cast<double>(integer);
    return true;
}

bool GenICamNodeMap::assignNumber(const std::string& name, double value)
{
    if(++depth > kMaxDepth)
    {
        return fail("feature references are nested too deeply at " + name);
    }
    struct DepthGuard
    {
        int& depth;
        ~DepthGuard(void)
        {
            --depth;
        }
    } guard{depth};

    const auto it = nodes.find(name);
    if(it == nodes.end())
    {
        return fail("no feature " + name);
    }
    Node& node = it->second;
    const auto pValue = node.fields.find("pValue");
    const bool hasPointer = pValue != node.fields.end() && !pValue->second.empty();

    if(node.type == "FloatReg")
    {
        return writeFloatRegister(node, value);
    }
    if(node.type == "Float")
    {
        if(hasPointer)
        {
            return assignNumber(pValue->second.front(), value);
        }
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.17g", value);
        node.fields["Value"] = {buffer};
        return true;
    }
    if(node.type == "Converter" || node.type == "IntConverter")
    {
        const auto formula = node.fields.find("FormulaTo");
        double raw = 0;
        if(!hasPointer || formula == node.fields.end()
           || !evaluateFormula(node, formula->second.front(),
                               {
                                   {"FROM", value}
        },
                               node.type == "IntConverter", &raw))
        {
            return hasPointer && formula != node.fields.end() ? false : fail(name + " is incomplete");
        }
        return assignNumber(pValue->second.front(), raw);
    }
    return assign(name, static_cast<std::int64_t>(std::llround(value)));
}

bool GenICamNodeMap::readFloatRegister(const Node& node, double* value)
{
    std::uint64_t address = 0;
    std::size_t length = 0;
    if(!registerAddress(node, &address) || !registerLength(node, &length))
    {
        return false;
    }
    if(length != 4 && length != 8)
    {
        return fail("unsupported float register length " + std::to_string(length));
    }
    std::uint8_t bytes[8] = {};
    if(!port || !port->read(address, bytes, length))
    {
        return fail("cannot read register at address " + std::to_string(address));
    }

    const auto endianess = node.fields.find("Endianess");
    const bool bigEndian = endianess != node.fields.end() && endianess->second.front() == "BigEndian";
    std::uint64_t raw = 0;
    for(std::size_t i = 0; i < length; ++i)
    {
        raw |= static_cast<std::uint64_t>(bytes[i]) << (bigEndian ? 8 * (length - 1 - i) : 8 * i);
    }
    if(length == 4)
    {
        const auto bits = static_cast<std::uint32_t>(raw);
        float single = 0;
        std::memcpy(&single, &bits, sizeof(single));
        *value = single;
    }
    else
    {
        std::memcpy(value, &raw, sizeof(*value));
    }
    return true;
}

bool GenICamNodeMap::writeFloatRegister(const Node& node, double value)
{
    std::uint64_t address = 0;
    std::size_t length = 0;
    if(!registerAddress(node, &address) || !registerLength(node, &length))
    {
        return false;
    }
    if(length != 4 && length != 8)
    {
        return fail("unsupported float register length " + std::to_string(length));
    }

    std::uint64_t raw = 0;
    if(length == 4)
    {
        const auto single = static_cast<float>(value);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &single, sizeof(bits));
        raw = bits;
    }
    else
    {
        std::memcpy(&raw, &value, sizeof(raw));
    }

    const auto endianess = node.fields.find("Endianess");
    const bool bigEndian = endianess != node.fields.end() && endianess->second.front() == "BigEndian";
    std::uint8_t bytes[8] = {};
    for(std::size_t i = 0; i < length; ++i)
    {
        bytes[i] = static_cast<std::uint8_t>((raw >> (bigEndian ? 8 * (length - 1 - i) : 8 * i)) & 0xFF);
    }
    if(!port || !port->write(address, bytes, length))
    {
        return fail("cannot write register at address " + std::to_string(address));
    }
    return true;
}

bool GenICamNodeMap::registerAddress(const Node& node, std::uint64_t* address)
{
    std::int64_t total = 0;
    const auto addresses = node.fields.find("Address");
    if(addresses != node.fields.end())
    {
        for(const std::string& text : addresses->second)
        {
            std::int64_t part = 0;
            if(!parseNumber(text, &part))
            {
                return fail("invalid register address " + text);
            }
            total += part;
        }
    }
    const auto pointers = node.fields.find("pAddress");
    if(pointers != node.fields.end())
    {
        for(const std::string& reference : pointers->second)
        {
            std::int64_t part = 0;
            if(!evaluate(reference, &part))
            {
                return false;
            }
            total += part;
        }
    }
    for(const auto& [indexNode, offsetText] : node.indexes)
    {
        std::int64_t index = 0;
        std::int64_t offset = 0;
        if(!evaluate(indexNode, &index))
        {
            return false;
        }
        if(!offsetText.empty() && offsetText[0] == '@')
        {
            if(!evaluate(offsetText.substr(1), &offset))
            {
                return false;
            }
        }
        else if(!parseNumber(offsetText, &offset))
        {
            return fail("invalid pIndex offset " + offsetText);
        }
        total += index * offset;
    }
    *address = static_cast<std::uint64_t>(total);
    return true;
}

bool GenICamNodeMap::registerLength(const Node& node, std::size_t* length)
{
    std::int64_t value = 4;
    const auto fixed = node.fields.find("Length");
    const auto pointer = node.fields.find("pLength");
    if(fixed != node.fields.end())
    {
        parseNumber(fixed->second.front(), &value);
    }
    else if(pointer != node.fields.end() && !evaluate(pointer->second.front(), &value))
    {
        return false;
    }
    if(value < 1 || value > 8)
    {
        return fail("unsupported register length " + std::to_string(value));
    }
    *length = static_cast<std::size_t>(value);
    return true;
}

bool GenICamNodeMap::readRegister(const Node& node, std::int64_t* value)
{
    std::uint64_t address = 0;
    std::size_t length = 0;
    if(!registerAddress(node, &address) || !registerLength(node, &length))
    {
        return false;
    }
    if(!port)
    {
        return fail("no camera connection");
    }

    std::uint8_t bytes[8] = {};
    if(!port->read(address, bytes, length))
    {
        return fail("cannot read register at address " + std::to_string(address));
    }

    const auto endianess = node.fields.find("Endianess");
    const bool bigEndian = endianess != node.fields.end() && endianess->second.front() == "BigEndian";
    std::uint64_t raw = 0;
    for(std::size_t i = 0; i < length; ++i)
    {
        const std::size_t shift = bigEndian ? 8 * (length - 1 - i) : 8 * i;
        raw |= static_cast<std::uint64_t>(bytes[i]) << shift;
    }

    const unsigned bits = static_cast<unsigned>(8 * length);
    unsigned low = 0;
    unsigned high = bits - 1;
    if(node.type == "MaskedIntReg")
    {
        const auto toPosition = [bigEndian, bits](std::int64_t bit) -> unsigned
        { return bigEndian ? bits - 1 - static_cast<unsigned>(bit) : static_cast<unsigned>(bit); };
        std::int64_t lsb = 0;
        std::int64_t msb = 0;
        if(node.fields.count("Bit"))
        {
            parseNumber(node.fields.at("Bit").front(), &lsb);
            msb = lsb;
        }
        else
        {
            parseNumber(node.fields.count("LSB") ? node.fields.at("LSB").front() : "0", &lsb);
            parseNumber(node.fields.count("MSB") ? node.fields.at("MSB").front() : "0", &msb);
        }
        low = std::min(toPosition(lsb), toPosition(msb));
        high = std::max(toPosition(lsb), toPosition(msb));
    }

    const unsigned width = high - low + 1;
    std::uint64_t field = raw >> low;
    if(width < 64)
    {
        field &= (std::uint64_t(1) << width) - 1;
    }
    const auto sign = node.fields.find("Sign");
    const bool isSigned = sign != node.fields.end() && sign->second.front() == "Signed";
    if(isSigned && width < 64 && (field >> (width - 1)) & 1)
    {
        field |= ~((std::uint64_t(1) << width) - 1);
    }
    *value = static_cast<std::int64_t>(field);
    return true;
}

bool GenICamNodeMap::writeRegister(const Node& node, std::int64_t value)
{
    std::uint64_t address = 0;
    std::size_t length = 0;
    if(!registerAddress(node, &address) || !registerLength(node, &length))
    {
        return false;
    }
    if(!port)
    {
        return fail("no camera connection");
    }

    const auto endianess = node.fields.find("Endianess");
    const bool bigEndian = endianess != node.fields.end() && endianess->second.front() == "BigEndian";
    const unsigned bits = static_cast<unsigned>(8 * length);
    std::uint8_t bytes[8] = {};
    std::uint64_t raw = static_cast<std::uint64_t>(value);

    if(node.type == "MaskedIntReg")
    {
        const auto toPosition = [bigEndian, bits](std::int64_t bit) -> unsigned
        { return bigEndian ? bits - 1 - static_cast<unsigned>(bit) : static_cast<unsigned>(bit); };
        std::int64_t lsb = 0;
        std::int64_t msb = 0;
        if(node.fields.count("Bit"))
        {
            parseNumber(node.fields.at("Bit").front(), &lsb);
            msb = lsb;
        }
        else
        {
            parseNumber(node.fields.count("LSB") ? node.fields.at("LSB").front() : "0", &lsb);
            parseNumber(node.fields.count("MSB") ? node.fields.at("MSB").front() : "0", &msb);
        }
        const unsigned low = std::min(toPosition(lsb), toPosition(msb));
        const unsigned width = std::max(toPosition(lsb), toPosition(msb)) - low + 1;
        const std::uint64_t mask = (width < 64 ? (std::uint64_t(1) << width) - 1 : ~std::uint64_t(0)) << low;

        std::uint64_t current = 0;
        if(port->read(address, bytes, length))
        {
            for(std::size_t i = 0; i < length; ++i)
            {
                current |= static_cast<std::uint64_t>(bytes[i]) << (bigEndian ? 8 * (length - 1 - i) : 8 * i);
            }
        }
        raw = (current & ~mask) | ((static_cast<std::uint64_t>(value) << low) & mask);
    }

    for(std::size_t i = 0; i < length; ++i)
    {
        const std::size_t shift = bigEndian ? 8 * (length - 1 - i) : 8 * i;
        bytes[i] = static_cast<std::uint8_t>((raw >> shift) & 0xFF);
    }
    if(!port->write(address, bytes, length))
    {
        return fail("cannot write register at address " + std::to_string(address));
    }
    return true;
}

bool GenICamNodeMap::fail(const std::string& message)
{
    lastError = message;
    return false;
}


bool parseGenICamUrl(const std::string& url, std::string* fileName, std::uint64_t* address, std::uint64_t* size,
                     std::string* error)
{
    std::string lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if(lower.rfind("local:", 0) != 0)
    {
        if(error)
        {
            *error = "the camera's GenICam file is not stored in the camera (" + url
                + "); this URL type is not supported";
        }
        return false;
    }

    std::string rest = url.substr(6);
    while(!rest.empty() && rest[0] == '/')
    {
        rest.erase(0, 1);
    }
    const std::size_t first = rest.find(';');
    const std::size_t second = first == std::string::npos ? std::string::npos : rest.find(';', first + 1);
    if(second == std::string::npos)
    {
        if(error)
        {
            *error = "malformed GenICam URL: " + url;
        }
        return false;
    }
    std::size_t end = rest.find_first_of("?;", second + 1);
    if(end == std::string::npos)
    {
        end = rest.size();
    }

    *fileName = rest.substr(0, first);
    if(!parseUnsigned(rest.substr(first + 1, second - first - 1), 16, address)
       || !parseUnsigned(rest.substr(second + 1, end - second - 1), 16, size))
    {
        if(error)
        {
            *error = "malformed GenICam URL: " + url;
        }
        return false;
    }
    return true;
}

bool extractXmlFromZip(const std::string& zip, std::string* xml, std::string* error)
{
    const auto failWith = [error](const std::string& message)
    {
        if(error)
        {
            *error = "GenICam ZIP: " + message;
        }
        return false;
    };

    constexpr std::uint32_t kEndSignature = 0x06054b50;
    constexpr std::uint32_t kCentralSignature = 0x02014b50;
    constexpr std::uint32_t kLocalSignature = 0x04034b50;
    std::size_t endRecord = std::string::npos;
    for(std::size_t i = zip.size() >= 22 ? zip.size() - 22 : 0; i != std::string::npos; --i)
    {
        std::uint32_t signature = 0;
        if(readUint32Le(zip, i, &signature) && signature == kEndSignature)
        {
            endRecord = i;
            break;
        }
        if(i == 0)
        {
            break;
        }
    }
    if(endRecord == std::string::npos)
    {
        return failWith("not a ZIP archive");
    }

    std::uint32_t entryCount = 0;
    std::uint32_t directoryOffset = 0;
    readUint16Le(zip, endRecord + 10, &entryCount);
    readUint32Le(zip, endRecord + 16, &directoryOffset);

    std::size_t position = directoryOffset;
    for(std::uint32_t i = 0; i < entryCount; ++i)
    {
        std::uint32_t signature = 0;
        std::uint32_t method = 0;
        std::uint32_t compressedSize = 0;
        std::uint32_t size = 0;
        std::uint32_t nameLength = 0;
        std::uint32_t extraLength = 0;
        std::uint32_t commentLength = 0;
        std::uint32_t localOffset = 0;
        if(!readUint32Le(zip, position, &signature) || signature != kCentralSignature
           || !readUint16Le(zip, position + 10, &method) || !readUint32Le(zip, position + 20, &compressedSize)
           || !readUint32Le(zip, position + 24, &size) || !readUint16Le(zip, position + 28, &nameLength)
           || !readUint16Le(zip, position + 30, &extraLength) || !readUint16Le(zip, position + 32, &commentLength)
           || !readUint32Le(zip, position + 42, &localOffset) || position + 46 + nameLength > zip.size())
        {
            return failWith("damaged central directory");
        }
        std::string name = zip.substr(position + 46, nameLength);
        position += 46 + nameLength + extraLength + commentLength;

        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if(name.size() < 4 || name.compare(name.size() - 4, 4, ".xml") != 0)
        {
            continue;
        }

        std::uint32_t localSignature = 0;
        std::uint32_t localNameLength = 0;
        std::uint32_t localExtraLength = 0;
        if(!readUint32Le(zip, localOffset, &localSignature) || localSignature != kLocalSignature
           || !readUint16Le(zip, localOffset + 26, &localNameLength)
           || !readUint16Le(zip, localOffset + 28, &localExtraLength))
        {
            return failWith("damaged file header");
        }
        const std::size_t dataOffset = localOffset + 30 + localNameLength + localExtraLength;
        if(dataOffset + compressedSize > zip.size())
        {
            return failWith("truncated archive");
        }

        if(method == 0)
        {
            *xml = zip.substr(dataOffset, compressedSize);
            return true;
        }
        if(method == 8)
        {
            if(!inflateRaw(zip.data() + dataOffset, compressedSize, size, xml))
            {
                return failWith("cannot decompress " + name);
            }
            return true;
        }
        return failWith("unsupported compression method " + std::to_string(method));
    }
    return failWith("no .xml file inside");
}


} // namespace calib
