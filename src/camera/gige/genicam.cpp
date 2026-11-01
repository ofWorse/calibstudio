#include "camera/gige/genicam.h"

#include <QByteArray>
#include <QString>
#include <QXmlStreamReader>

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <stdlib.h>
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


// Integer expression evaluator for SwissKnife formulas: C-like operators plus
// GenICam's "=" (equal), "<>" (not equal) and "**" (power).
class FormulaParser
{
private:
    const std::string& text;
    std::size_t position = 0;
    std::function<bool(const std::string&, std::int64_t*)> lookup;
    bool ok = true;

public:
    FormulaParser(const std::string& text, std::function<bool(const std::string&, std::int64_t*)> lookup)
        : text(text),
          lookup(std::move(lookup))
    {
    }

    bool parse(std::int64_t* value)
    {
        *value = ternary();
        skipSpace();
        return ok && position == text.size();
    }

private:
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

    std::int64_t ternary(void)
    {
        const std::int64_t condition = logicalOr();
        if(accept("?"))
        {
            const std::int64_t whenTrue = ternary();
            if(!accept(":"))
            {
                ok = false;
                return 0;
            }
            const std::int64_t whenFalse = ternary();
            return condition ? whenTrue : whenFalse;
        }
        return condition;
    }

    std::int64_t logicalOr(void)
    {
        std::int64_t value = logicalAnd();
        while(accept("||"))
        {
            const std::int64_t right = logicalAnd();
            value = (value || right) ? 1 : 0;
        }
        return value;
    }

    std::int64_t logicalAnd(void)
    {
        std::int64_t value = bitOr();
        while(accept("&&"))
        {
            const std::int64_t right = bitOr();
            value = (value && right) ? 1 : 0;
        }
        return value;
    }

    std::int64_t bitOr(void)
    {
        std::int64_t value = bitXor();
        while(acceptUnless("|", "||"))
        {
            value |= bitXor();
        }
        return value;
    }

    std::int64_t bitXor(void)
    {
        std::int64_t value = bitAnd();
        while(accept("^"))
        {
            value ^= bitAnd();
        }
        return value;
    }

    std::int64_t bitAnd(void)
    {
        std::int64_t value = equality();
        while(acceptUnless("&", "&&"))
        {
            value &= equality();
        }
        return value;
    }

    std::int64_t equality(void)
    {
        std::int64_t value = comparison();
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

    std::int64_t comparison(void)
    {
        std::int64_t value = shift();
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

    std::int64_t shift(void)
    {
        std::int64_t value = additive();
        while(true)
        {
            if(accept("<<"))
            {
                value = static_cast<std::int64_t>(static_cast<std::uint64_t>(value) << (additive() & 63));
            }
            else if(accept(">>"))
            {
                value = static_cast<std::int64_t>(static_cast<std::uint64_t>(value) >> (additive() & 63));
            }
            else
            {
                return value;
            }
        }
    }

    std::int64_t additive(void)
    {
        std::int64_t value = multiplicative();
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

    std::int64_t multiplicative(void)
    {
        std::int64_t value = power();
        while(true)
        {
            if(acceptUnless("*", "**"))
            {
                value *= power();
            }
            else if(accept("/"))
            {
                const std::int64_t divisor = power();
                value = divisor != 0 ? value / divisor : (ok = false, 0);
            }
            else if(accept("%"))
            {
                const std::int64_t divisor = power();
                value = divisor != 0 ? value % divisor : (ok = false, 0);
            }
            else
            {
                return value;
            }
        }
    }

    std::int64_t power(void)
    {
        const std::int64_t base = unary();
        if(accept("**"))
        {
            const std::int64_t exponent = power();
            std::int64_t result = 1;
            for(std::int64_t i = 0; i < exponent && i < 64; ++i)
            {
                result *= base;
            }
            return result;
        }
        return base;
    }

    std::int64_t unary(void)
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
            return ~unary();
        }
        if(acceptUnless("!", "!="))
        {
            return unary() ? 0 : 1;
        }
        return primary();
    }

    std::int64_t primary(void)
    {
        skipSpace();
        if(accept("("))
        {
            const std::int64_t value = ternary();
            if(!accept(")"))
            {
                ok = false;
            }
            return value;
        }

        const std::size_t start = position;
        if(position < text.size() && std::isdigit(static_cast<unsigned char>(text[position])))
        {
            while(position < text.size() && std::isalnum(static_cast<unsigned char>(text[position])))
            {
                ++position;
            }
            std::int64_t value = 0;
            if(!parseNumber(text.substr(start, position - start), &value))
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
        std::int64_t value = 0;
        if(!lookup(text.substr(start, position - start), &value))
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
    if(node->type == "IntSwissKnife" || node->type == "SwissKnife")
    {
        const std::string* formula = field("Formula");
        return formula ? evaluateFormula(*node, *formula, value) : fail(name + " has no formula");
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

bool GenICamNodeMap::evaluateFormula(const Node& node, const std::string& formula, std::int64_t* value)
{
    const auto lookup = [this, &node](const std::string& variable, std::int64_t* result) -> bool
    {
        const auto reference = node.variables.find(variable);
        if(reference != node.variables.end())
        {
            return evaluate(reference->second, result);
        }
        const auto constant = node.constants.find(variable);
        if(constant != node.constants.end())
        {
            return parseNumber(constant->second, result) || evaluateFormula(node, constant->second, result);
        }
        return fail("unknown variable " + variable + " in formula");
    };

    FormulaParser parser(formula, lookup);
    if(!parser.parse(value))
    {
        return fail(lastError.empty() ? "cannot evaluate formula " + formula : lastError);
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
