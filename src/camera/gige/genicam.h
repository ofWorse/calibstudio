#ifndef CALIBSTUDIO_GENICAM_H
#define CALIBSTUDIO_GENICAM_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace calib
{


// Register access the node map needs from the camera (the GenICam "port").
class IRegisterPort
{
public:
    virtual ~IRegisterPort(void) = default;
    virtual bool read(std::uint64_t address, std::uint8_t* data, std::size_t size) = 0;
    virtual bool write(std::uint64_t address, const std::uint8_t* data, std::size_t size) = 0;
};


class GenICamNodeMap
{
private:
    struct Node
    {
        std::string type;
        std::map<std::string, std::vector<std::string>> fields;
        std::map<std::string, std::string> variables;
        std::map<std::string, std::string> constants;
        std::vector<std::pair<std::string, std::int64_t>> entries;
        std::vector<std::pair<std::string, std::string>> indexes;
    };

    std::unordered_map<std::string, Node> nodes;
    IRegisterPort* port = nullptr;
    std::string lastError;
    int depth = 0;

public:
    bool load(const std::string& xml, std::string* error);
    void setPort(IRegisterPort* port);

    bool has(const std::string& name) const;
    std::size_t size(void) const
    {
        return nodes.size();
    }
    const std::string& getLastError(void) const
    {
        return lastError;
    }

    bool getInteger(const std::string& name, std::int64_t* value);
    bool setInteger(const std::string& name, std::int64_t value);
    bool execute(const std::string& name);
    bool getEnumeration(const std::string& name, std::string* entry);
    bool setEnumeration(const std::string& name, const std::string& entry);

    bool getNumber(const std::string& name, double* value);
    bool setNumber(const std::string& name, double value);
    bool getRange(const std::string& name, double* minimum, double* maximum);
    bool isFloat(const std::string& name) const;
    std::vector<std::string> enumerationEntries(const std::string& name) const;

private:
    const Node* find(const std::string& name);
    bool evaluate(const std::string& name, std::int64_t* value);
    bool assign(const std::string& name, std::int64_t value);
    bool evaluateNumber(const std::string& name, double* value);
    bool assignNumber(const std::string& name, double value);
    bool evaluateFormula(const Node& node, const std::string& formula, const std::map<std::string, double>& extra,
                         bool integer, double* value);
    bool readFloatRegister(const Node& node, double* value);
    bool writeFloatRegister(const Node& node, double value);
    bool registerAddress(const Node& node, std::uint64_t* address);
    bool registerLength(const Node& node, std::size_t* length);
    bool readRegister(const Node& node, std::int64_t* value);
    bool writeRegister(const Node& node, std::int64_t value);
    bool fail(const std::string& message);
};


// "Local:camera.zip;8000000;1A2B" -> file name, address, size. Returns false
// for URLs that point outside the camera (file: or http:).
bool parseGenICamUrl(const std::string& url, std::string* fileName, std::uint64_t* address, std::uint64_t* size,
                     std::string* error);

// The XML file stored in a ZIP archive (cameras usually store it zipped).
bool extractXmlFromZip(const std::string& zip, std::string* xml, std::string* error);


} // namespace calib

#endif // CALIBSTUDIO_GENICAM_H
