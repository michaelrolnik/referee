/*
 *  MIT License
 *  
 *  Copyright (c) 2022 Michael Rolnik
 *  
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *  
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include "database.hpp"

#include <stdint.h>
#include <string>
#include <fstream>
#include <chrono>
#include <iostream>
#include <iomanip>

#include <arpa/inet.h>

#include "rapidcsv.h"
#include "utils.hpp"

#ifndef __APPLE__
constexpr auto htonll(int64_t h)
{
    if (std::endian::native != std::endian::big) {
        static_assert(CHAR_BIT==8);
        constexpr auto shift_bytes1{8};
        constexpr auto shift_bytes2{16};
        constexpr auto shift_bytes4{32};
        h = ((h&UINT64_C(0x00FF00FF00FF00FF))<<shift_bytes1) | ((h&UINT64_C(0xFF00FF00FF00FF00))>>shift_bytes1);
        h = ((h&UINT64_C(0x0000FFFF0000FFFF))<<shift_bytes2) | ((h&UINT64_C(0xFFFF0000FFFF0000))>>shift_bytes2);
        h = ((h&UINT64_C(0x00000000FFFFFFFF))<<shift_bytes4) | ((h&UINT64_C(0xFFFFFFFF00000000))>>shift_bytes4);
    }
    return h;
}

constexpr auto ntohll(int64_t h)
{
    if (std::endian::native != std::endian::big) {
        static_assert(CHAR_BIT==8);
        constexpr auto shift_bytes1{8};
        constexpr auto shift_bytes2{16};
        constexpr auto shift_bytes4{32};
        h = ((h&UINT64_C(0x00FF00FF00FF00FF))<<shift_bytes1) | ((h&UINT64_C(0xFF00FF00FF00FF00))>>shift_bytes1);
        h = ((h&UINT64_C(0x0000FFFF0000FFFF))<<shift_bytes2) | ((h&UINT64_C(0xFFFF0000FFFF0000))>>shift_bytes2);
        h = ((h&UINT64_C(0x00000000FFFFFFFF))<<shift_bytes4) | ((h&UINT64_C(0xFFFFFFFF00000000))>>shift_bytes4);
    }
    return h;
}
#endif

namespace referee::db {

class DataWriter::Impl
{
public:
    virtual ~Impl() {};

    virtual void        integer(    int64_t             data) = 0;
    virtual void        number(     double              data) = 0;
    virtual void        boolean(    bool                data) = 0;
    virtual void        string(     std::string const&  data) = 0;
    virtual void        size(       unsigned            size) = 0;
    virtual std::string build() = 0;
};

class DataWriterPlain
    : public DataWriter::Impl
{
public:
    DataWriterPlain();

    void        integer(    int64_t             data) override;
    void        number(     double              data) override;
    void        boolean(    bool                data) override;
    void        string(     std::string const&  data) override;
    void        size(       unsigned            size) override;
    std::string build() override;

private:
    std::ostringstream  m_os;
};

class DataWriterTyped
    : public DataWriter::Impl
{
public:
    DataWriterTyped(Type* type);

    void        integer(    int64_t             data) override;
    void        number(     double              data) override;
    void        boolean(    bool                data) override;
    void        string(     std::string const&  data) override;
    void        size(       unsigned            size) override;
    std::string build() override;

private:
    template<typename Type>
    Type*       pop_type();

private:
    std::deque<Type*>   m_type;
    DataWriterPlain     m_writer;
};

class DataReader::Impl
{
public:
    virtual ~Impl() {};

    virtual void        integer(    int64_t&            data) = 0;
    virtual void        number(     double&             data) = 0;
    virtual void        boolean(    bool&               data) = 0;
    virtual void        string(     std::string&        data) = 0;
    virtual void        size(       unsigned&           size) = 0;
    virtual void        done() = 0;
};

class DataReaderPlain
    : public DataReader::Impl
{
public:
    DataReaderPlain(std::string const& data);

    void        integer(    int64_t&            data) override;
    void        number(     double&             data) override;
    void        boolean(    bool&               data) override;
    void        string(     std::string&        data) override;
    void        size(       unsigned&           size) override;
    void        done() override;

private:
    std::istringstream  m_is;
};

class DataReaderTyped
    : public DataReader::Impl
{
public:
    DataReaderTyped(std::string const& data, Type* type);

    void        integer(    int64_t&            data) override;
    void        number(     double&             data) override;
    void        boolean(    bool&               data) override;
    void        string(     std::string&        data) override;
    void        size(       unsigned&           size) override;
    void        done() override;

private:
    template<typename Type>
    Type*       pop_type();

private:
    std::deque<Type*>   m_type;
    DataReaderPlain     m_reader;
};

DataWriterTyped::DataWriterTyped(Type* type)
{
    m_type.push_back(type);
}

template<typename Type>
Type*           DataWriterTyped::pop_type()
{
    if(m_type.empty())
        throw   std::runtime_error("wrong data/type");

    if(auto record = dynamic_cast<TypeRecord*>(m_type.front()))
    {
        m_type.pop_front();

        for(auto it = record->body().rbegin(); it != record->body().rend(); it ++)
        {
            m_type.push_front(it->type);
        }
    }

    if(m_type.empty())
        throw   std::runtime_error("wrong data/type");

    auto    type    = dynamic_cast<Type*>(m_type.front());

    if(type == nullptr)
        throw   std::runtime_error("wrong data/type");

    m_type.pop_front();

    return type;
}

void    DataWriterTyped::integer(   int64_t             data)
{
    pop_type<TypeInteger>();

    m_writer.integer(data);
}

void    DataWriterTyped::number(    double              data)
{
    pop_type<TypeNumber>();

    m_writer.number(data);
}

void    DataWriterTyped::boolean(   bool                data)
{
    pop_type<TypeBoolean>();

    m_writer.boolean(data);
}

void    DataWriterTyped::string(    std::string const&  data)
{
    pop_type<TypeString>();

    m_writer.string(data);
}

void    DataWriterTyped::size(      unsigned            size)
{
    auto*   type    = pop_type<TypeArray>();

    if(type->size != 0 && type->size != size)
        throw   std::runtime_error("invalid array size");

    m_writer.size(size);

    for(auto i = 0u; i < size; i++)
    {
        m_type.push_front(type->base);
    }
}

std::string     DataWriterTyped::build()
{
    return  m_writer.build();
}

DataWriterPlain::DataWriterPlain()
{
}

void    DataWriterPlain::integer(   int64_t             data)
{
    auto    buff    = htonll(data);
    m_os.write(reinterpret_cast<char const*>(&buff), sizeof(buff));
}

void    DataWriterPlain::number(    double              data)
{
    auto    buff    = htonll(*reinterpret_cast<uint64_t*>(&data));
    m_os.write(reinterpret_cast<char const*>(&buff), sizeof(buff));
}

void    DataWriterPlain::boolean(   bool                data)
{
    m_os.write(reinterpret_cast<char const*>(&data), sizeof(data));
}

void    DataWriterPlain::string(    std::string const&  data)
{
    auto    size    = htonl(data.size());
    m_os.write(reinterpret_cast<char const*>(&size), sizeof(size));
    m_os.write(reinterpret_cast<char const*>(data.data()), data.size());
}

void    DataWriterPlain::size(      unsigned            size)
{
    auto    buff    = htonl(size);
    m_os.write(reinterpret_cast<char const*>(&buff), sizeof(buff));
}

std::string     DataWriterPlain::build()
{
    return  m_os.str();
}

DataWriter::DataWriter()
    : m_impl(new DataWriterPlain())
{
}

DataWriter::DataWriter(Type* type)
    : m_impl(new DataWriterTyped(type))
{
}

DataWriter::~DataWriter() = default;

DataWriter&    DataWriter::integer( int64_t             data)
{
    m_impl->integer(data);
    return *this;
}

DataWriter&    DataWriter::number(  double              data)
{
    m_impl->number(data);
    return *this;
}

DataWriter&    DataWriter::boolean( bool                data)
{
    m_impl->boolean(data);
    return *this;
}

DataWriter&    DataWriter::string(  std::string const&  data)
{
    m_impl->string(data);
    return *this;
}

DataWriter&    DataWriter::size(    unsigned            size)
{
    m_impl->size(size);
    return *this;
}

std::string     DataWriter::build()
{
    return  m_impl->build();
}

DataReader::DataReader( std::string const&  data)
    : m_impl(new DataReaderPlain(data))
{
}

DataReader::DataReader( std::string const&  data,
                        Type*               type)
    : m_impl(new DataReaderTyped(data, type))
{
}

DataReader::~DataReader() = default;

DataReader& DataReader::integer(    int64_t&            data)
{
    m_impl->integer(data);

    return *this;
}

DataReader& DataReader::number(     double&             data)
{
    m_impl->number(data);

    return *this;
}
DataReader& DataReader::boolean(    bool&               data)
{
    m_impl->boolean(data);

    return *this;
}
DataReader& DataReader::string(     std::string&        data)
{
    m_impl->string(data);

    return *this;
}
DataReader& DataReader::size(       unsigned&           size)
{
    m_impl->size(size);

    return *this;
}

void        DataReader::done()
{
    m_impl->done();
}


DataReaderTyped::DataReaderTyped(std::string const& data, Type* type)
    : m_reader(data)
{
    m_type.push_back(type);
}

template<typename Type>
Type*           DataReaderTyped::pop_type()
{
    if(m_type.empty())
        throw   std::runtime_error("wrong data/type");

    if(auto record = dynamic_cast<TypeRecord*>(m_type.front()))
    {
        m_type.pop_front();

        for(auto it = record->body().rbegin(); it != record->body().rend(); it ++)
        {
            m_type.push_front(it->type);
        }
    }

    if(m_type.empty())
        throw   std::runtime_error("wrong data/type");

    auto    type    = dynamic_cast<Type*>(m_type.front());

    if(type == nullptr)
        throw   std::runtime_error("wrong data/type");

    m_type.pop_front();

    return type;
}

void    DataReaderTyped::integer(   int64_t&            data)
{
    pop_type<TypeInteger>();

    m_reader.integer(data);
}

void    DataReaderTyped::number(    double&             data)
{
    pop_type<TypeNumber>();

    m_reader.number(data);
}

void    DataReaderTyped::boolean(   bool&               data)
{
    pop_type<TypeBoolean>();

    m_reader.boolean(data);
}

void    DataReaderTyped::string(    std::string&        data)
{
    pop_type<TypeString>();

    m_reader.string(data);
}

void    DataReaderTyped::size(      unsigned&           size)
{
    auto*   type    = pop_type<TypeArray>();

    m_reader.size(size);

    if(type->size != 0 && type->size != size)
        throw   std::runtime_error("invalid array size");

    for(auto i = 0u; i < size; i++)
    {
        m_type.push_front(type->base);
    }
}

void    DataReaderTyped::done()
{
    m_reader.done();
}

DataReaderPlain::DataReaderPlain(std::string const& data)
    : m_is(data)
{
}

void    DataReaderPlain::integer(   int64_t&            data)
{
    int64_t buff;
    m_is.read(reinterpret_cast<char*>(&buff), sizeof(buff));
    data    = ntohll(buff);
}

void    DataReaderPlain::number(    double&             data)
{
    auto    buff    = htonll(*reinterpret_cast<uint64_t*>(&data));
    m_is.read(reinterpret_cast<char*>(&buff), sizeof(buff));
    *reinterpret_cast<uint64_t*>(&data) = ntohll(buff);
}

void    DataReaderPlain::boolean(   bool&               data)
{
    m_is.read(reinterpret_cast<char*>(&data), sizeof(data));
}

void    DataReaderPlain::string(    std::string&        data)
{
    uint32_t    size;
    m_is.read(reinterpret_cast<char*>(&size), sizeof(size));
    size    = ntohl(size);
    data.resize(size);
    m_is.read(reinterpret_cast<char*>(data.data()), data.size());
}

void    DataReaderPlain::size(      unsigned&           size)
{
    unsigned    buff;
    m_is.read(reinterpret_cast<char*>(&buff), sizeof(buff));
    size    = ntohl(buff);
}

void    DataReaderPlain::done()
{
    //  TODO: assert m_is is empty
}

#if 0
struct RecordInfo 
{
    uint64_t    timestamp;
    uint32_t    size;    
    uint8_t     type;
    uint8_t     subtype;
};

class   Database
{
public:
    Database();

    void    open(std::string filename);
    void    close();

    void    write(uint64_t timestamp, uint16_t type, uint16_t subtype, std::string data);

    bool    valid();
    uint64_t    timestamp() {return m_timestamp;}
    uint16_t    type()      {return m_type;}
    uint16_t    subtype()   {return m_subtype;}
    std::string data();

    void    next();
    void    reset();

private:
    std::fstream    m_file;
    uint64_t        m_size      = 0;
    uint64_t        m_timestamp;
    uint16_t        m_type;
    uint16_t        m_subtype;
    std::string     m_data;
    bool            m_has_data  = false;
};

Database::Database()
{
}

void    Database::open(std::string filename)
{
    m_file.open(filename, std::ios_base::binary | std::ios_base::in);
    next();
}

void    Database::close()
{
    m_file.flush();
    m_file.close();
}


void    Database::write(uint64_t timestamp, uint16_t type, uint16_t subtype, std::string data)
{
    uint64_t    size    = data.length();
    m_file.write(reinterpret_cast<char const*>(&size), sizeof(size));
    m_file.write(reinterpret_cast<char const*>(&type), sizeof(type));
    m_file.write(reinterpret_cast<char const*>(&subtype), sizeof(subtype));
    m_file.write(reinterpret_cast<char const*>(&timestamp), sizeof(timestamp));
    m_file.write(reinterpret_cast<char const*>(data.data()), data.length());
}

uint64_t    timestamp()
{
    return  std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}


bool    Database::valid()
{
    return m_file.good();
}

void    Database::next()
{
    uint64_t    size;

    if(m_has_data == false)
    {
        m_file.seekg(m_size, std::ios_base::cur);
    }

    m_file.read(reinterpret_cast<char*>(&m_size), sizeof(m_size));
    m_file.read(reinterpret_cast<char*>(&m_type), sizeof(m_type));
    m_file.read(reinterpret_cast<char*>(&m_subtype), sizeof(m_subtype));
    m_file.read(reinterpret_cast<char*>(&m_timestamp), sizeof(m_timestamp));
    m_has_data = false;
}

std::string Database::data()
{
    if(m_has_data)
        return m_data;

    m_data.resize(m_size);
    m_file.read(reinterpret_cast<char*>(m_data.data()), m_size);
    m_has_data = true;

    return m_data;
}


void    Database::reset()
{
    m_file.seekg(0, std::ios_base::beg);
}

int main(int argc, char* argv[])
{
    Database    db;
#if 0
    db.open("./database.rdb");

    db.write(timestamp(), 0, 0, "referee");
    db.write(timestamp(), 0, 1, "1.0.0");
    db.write(timestamp(), 1, 2, "hello");
    db.write(timestamp(), 1, 2, "we are here");
    db.write(timestamp(), 1, 2, "why");

    db.close();
#else
    db.open("./database.rdb");

    while(db.valid())
    {
        std::cout << db.timestamp() << " " << db.data() << std::endl;
        db.next();
    }
#endif

}



//TypeBuilder&    add_timestamp(  std::string const&  name);
TypeBuilder&    add_integer(    std::string const&  name);
TypeBuilder&    add_boolean(    std::string const&  name);
TypeBuilder&    add_number(     std::string const&  name);
TypeBuilder&    add_string(     std::string const&  name);
TypeBuilder&    enter_array(    std::string const&  name);
TypeBuilder&    enter_array(    std::string const&  name,
                                unsigned            size);
TypeBuilder&    leave_array();
TypeBuilder&    enter_struct(   std::string const&  name);
TypeBuilder&    leave_struct();
    
#endif

TypeBuilderRecord&  TypeBuilderRecord::integer(std::string name)
{
    m_body.push_back(Name2Type(name, new TypeInteger{}));
    return  *this;
}

TypeBuilderRecord&  TypeBuilderRecord::number( std::string name)
{
    m_body.push_back(Name2Type(name, new TypeNumber{}));

    return  *this;
}

TypeBuilderRecord&  TypeBuilderRecord::boolean(std::string name)
{
    m_body.push_back(Name2Type(name, new TypeBoolean{}));

    return  *this;
}

TypeBuilderRecord&  TypeBuilderRecord::string( std::string name)
{
    m_body.push_back(Name2Type(name, new TypeString{}));

    return  *this;
}

TypeBuilderRecord&  TypeBuilderRecord::record( std::string name, TypeRecord*type)
{
    m_body.push_back(Name2Type(name, type));

    return  *this;
}

TypeBuilderRecord&  TypeBuilderRecord::array(  std::string name, TypeArray* type)
{
    m_body.push_back(Name2Type(name, type));

    return  *this;
}

TypeRecord*         TypeBuilderRecord::build()
{
    return  new TypeRecord(m_body);
}

TypeBuilderArray&   TypeBuilderArray::integer()
{
    if(m_body != nullptr)
        throw std::logic_error("array body has been already specified");

    m_body  = new TypeInteger{};

    return  *this;
}

TypeBuilderArray&   TypeBuilderArray::number()
{
    if(m_body != nullptr)
        throw std::logic_error("array body has been already specified");

    m_body  = new TypeNumber{};

    return  *this;
}

TypeBuilderArray&   TypeBuilderArray::boolean()
{
    if(m_body != nullptr)
        throw std::logic_error("array body has been already specified");

    m_body  = new TypeBoolean{};

    return  *this;
}

TypeBuilderArray&   TypeBuilderArray::string()
{
    if(m_body != nullptr)
        throw std::logic_error("array body has been already specified");

    m_body  = new TypeString{};

    return  *this;
}

TypeBuilderArray&   TypeBuilderArray::record(   TypeRecord* type)
{
    if(m_body != nullptr)
        throw std::logic_error("array body has been already specified");

    m_body  = type;

    return  *this;
}

TypeBuilderArray&   TypeBuilderArray::array(    TypeArray*  type)
{
    if(m_body != nullptr)
        throw std::logic_error("array body has been already specified");

    m_body  = type;

    return  *this;
}

TypeBuilderArray&   TypeBuilderArray::size(   unsigned    size)
{
    m_size  = size;

    return *this;
}

TypeArray*          TypeBuilderArray::build()
{
    if(m_body == nullptr)
        throw std::logic_error("array body has not been yet specified");
    
    return  new TypeArray(m_body, m_size);
}


void printHelper(Type* const type, std::string prefix = "")
{
    if(nullptr != dynamic_cast<TypeInteger*>(type))
        std::cout << "\"type\": \"integer\"";

    if(nullptr != dynamic_cast<TypeBoolean*>(type))
        std::cout << "\"type\": \"boolean\"";
        
    if(nullptr != dynamic_cast<TypeNumber*>(type))
        std::cout << "\"type\": \"number\"";

    if(nullptr != dynamic_cast<TypeString*>(type))
        std::cout << "\"type\": \"string\"";

    if(auto array = dynamic_cast<TypeArray*>(type))
    {
        std::cout << "\"type\": \"array\", \"body\": {\"size\": "<< array->size << ", ";
        printHelper(array->base, prefix + "    ");
        std::cout << "}";
    }

    if(auto record = dynamic_cast<TypeRecord*>(type))
    {
        std::cout << "\"type\": \"record\", \"body\": [";

        auto    suffix   = "\n";
        for(auto& item: record->body())
        {
            std::cout << suffix << prefix << "    {\"name\": \"" << item.name << "\", ";
            printHelper(item.type, prefix + "    ");
            std::cout << "}";

            suffix = ",\n";
        }
        std::cout << "]";
    }
}

std::ostream&   printHex(std::string const& data)
{
    std::cout << std::endl;
    char    text[17]    = "                ";
    for(unsigned i = 0; i < data.size(); i++)
    {
        text[i % 16]    = isprint(data[i]) ? data[i] : '.';

        if(i % 16 == 0)
        {
            std::cout << std::hex << std::setw(4) << std::setfill('0') <<  i << ": ";
        }

        std::cout << std::hex << std::setw(2) << std::setfill('0') << (0xff & (unsigned)data[i]) << " ";
        if((i + 1) % 16 == 0)
        {
            std::cout << " | " << text << std::endl;
        }
    }

    if(data.size() % 16 != 0)
    {
        auto i = data.size() % 16;

        text[i] = 0;
        std::cout << std::setfill(' ') << std::setw(3 * (16 - i)) << " " << " | " << text << std::endl;
    }

    return std::cout;
}

void print(Type* const type)
{
    std::cout << "{";
    printHelper(type);
    std::cout << "}";
}

void    readData(Type* main, std::string const& data)
{
    std::deque<Type*>   type;
    std::vector<Type*>  fifo;
    std::istringstream  is(data);

    type.push_back(main);

    while(type.empty() == false)
    {
        Type*   top     = type.front();

        type.pop_front();

        if(dynamic_cast<TypeInteger*>(top))
        {
            uint64_t    buff;

            is.read(reinterpret_cast<char*>(&buff), sizeof(buff));

            buff    = ntohll(buff);
            std::cout << std::dec << buff << " ";
        }

        if(dynamic_cast<TypeNumber*>(top))
        {
            uint64_t    buff;

            is.read(reinterpret_cast<char*>(&buff), sizeof(buff));

            buff    = ntohll(buff);
            std::cout << std::dec << *reinterpret_cast<double*>(&buff) << " ";
        }    

        if(dynamic_cast<TypeBoolean*>(top))
        {
            bool    buff;

            is.read(reinterpret_cast<char*>(&buff), sizeof(buff));

            std::cout << buff << " ";
        }    

        if(dynamic_cast<TypeString*>(top))
        {
            unsigned    size;

            is.read(reinterpret_cast<char*>(&size), sizeof(size));
            size    = ntohl(size);

            std::string buff(size, 0);
            is.read(buff.data(), size);

            std::cout << "\"" << buff << "\" ";
        }   

        if(auto curr = dynamic_cast<TypeRecord*>(top))
        {
            for(auto it = curr->body().rbegin(); it != curr->body().rend(); it ++)
            {
                type.push_front(it->type);
            }
        }

        if(auto curr = dynamic_cast<TypeArray*>(top))
        {
            uint32_t    size;
            is.read(reinterpret_cast<char*>(&size), sizeof(size));
            size = ntohl(size);

            for(auto i = 0u; i < size; i++)
            {
                type.push_front(curr->base);
            }
        }
    }
}

void nameHelper(Type* type, std::string prefix, std::vector<std::string>& names)
{
    if(auto array = dynamic_cast<TypeArray*>(type))
    {
        auto    size    = array->size == 0 ? 1 : array->size;

        names.push_back(prefix + "#size");

        std::cout << prefix << "#size" << std::endl;
        for(auto i = 0u; i < size; i++)
        {
            nameHelper(array->base, prefix + "[" + std::to_string(i) + "]", names);
        }
    }

    else if(auto record = dynamic_cast<TypeRecord*>(type))
    {
        for(auto& item: record->body())
        {
            nameHelper(item.type, prefix + "." + item.name, names);
        }
    }
    else
    {
        if(nullptr != dynamic_cast<TypeInteger*>(type))
        {
            names.push_back(prefix + ":integer");

            std::cout << prefix << ":integer" << std::endl;
        }
        if(nullptr != dynamic_cast<TypeNumber*>(type))
        {
            names.push_back(prefix + ":number");

            std::cout << prefix << ":number" << std::endl;
        }
        if(nullptr != dynamic_cast<TypeBoolean*>(type))
        {
            names.push_back(prefix + ":boolean");

            std::cout << prefix << ":boolean" << std::endl;
        }
        if(nullptr != dynamic_cast<TypeString*>(type))
        {
            names.push_back(prefix + ":string");

            std::cout << prefix << ":string" << std::endl;
        }
    }
}

std::vector<std::string>    names(Type* type, std::string name)
{
    std::vector<std::string>    names;

    nameHelper(type, name, names);

    return names;
}

void readCsv(Type* type, std::string name, std::string data)
{
    rapidcsv::Document  doc(name);

    std::cout << std::endl;
/*
    auto cols = doc.GetColumnNames();

    for(auto col: cols)
    {
        std::cout << col << std::endl;
    }
*/
    //auto cols   = names(type, data);
    auto rows   = doc.GetRowCount();

    for(size_t row = 0; row < rows; row++)
    {
        std::deque<Type*>       types;
        std::deque<std::string> names;
        DataWriter             builder;

        types.push_front(type);
        names.push_front(data);

        while(types.empty() == false)
        {
            auto    type    = types.front();    types.pop_front();
            auto    name    = names.front();    names.pop_front();

            if(dynamic_cast<TypeInteger*>(type))
            {
                int64_t     data    = doc.GetCell<int64_t>(name, row);
                builder.integer(data);
            }

            if(dynamic_cast<TypeNumber*>(type))
            {
                double      data    = doc.GetCell<double>(name, row);
                builder.number(data);
            }

            if(dynamic_cast<TypeBoolean*>(type))
            {
                std::string data    = doc.GetCell<std::string>(name, row);
                bool        value   = false;
                if(data == "true" || data == "yes" || data == "1")
                    value   = true;
                if(data == "false" || data == "no" || data == "0")
                    value   = false;                    

                builder.boolean(value);
            }

            if(dynamic_cast<TypeString*>(type))
            {
                std::string data    = doc.GetCell<std::string>(name, row);
                builder.string(data);
            }

            if(auto curr = dynamic_cast<TypeRecord*>(type))
            {
                for(auto it = curr->body().rbegin(); it != curr->body().rend(); it ++)
                {
                    types.push_front(it->type);
                    names.push_front(name + "." + it->name);
                }
            }

            if(auto curr = dynamic_cast<TypeArray*>(type))
            {
                unsigned    size    = doc.GetCell<unsigned>(name + "#size", row);

                for(auto i = 0u; i < size; i++)
                {
                    types.push_front(curr->base);
                    names.push_front(name + "[" + std::to_string(size - i - 1) + "]");
                }

                builder.size(size);
            }
        }

        auto data = builder.build();

        printHex(data) << std::endl;
        readData(type, data);

/*
        for(auto col: cols)
        {
            std::cout << doc.GetCell<std::string>(col, row) << " ";
        }
        std::cout << std::endl;
*/

    }

}

void    Writer::open(std::string filename)
{
    m_os.open(filename, std::ios_base::binary | std::ios_base::in | std::ios_base::trunc);

    record(0x00010000, "referee");
    record(0x00010001, "v1.0.0");
}

void    Writer::close()
{
    m_os.flush();
    m_os.close();
}


void    Writer::record( uint32_t            info,
                        std::string const&  data)
{
    uint32_t    buff32;

    buff32  = htonl(info);
    m_os.write(reinterpret_cast<char const*>(&buff32), sizeof(buff32));

    buff32  = htonl(data.size());
    m_os.write(reinterpret_cast<char const*>(&buff32), sizeof(buff32));

    m_os.write(reinterpret_cast<char const*>(data.data()), data.size());
}

void    Writer::record( uint32_t            info,
                        uint64_t            time,
                        std::string const&  data)
{
    uint32_t    buff32;
    uint64_t    buff64;

    buff32  = htonl(info);
    m_os.write(reinterpret_cast<char const*>(&buff32), sizeof(buff32));

    buff32  = htonl(data.size() + sizeof(time));
    m_os.write(reinterpret_cast<char const*>(&buff32), sizeof(buff32));

    buff64  = htonll(time);
    m_os.write(reinterpret_cast<char const*>(&buff64), sizeof(buff64));

    m_os.write(reinterpret_cast<char const*>(data.data()), data.size());
}

void    encode(std::ostream& os, Type* const type, std::string prefix = "")
{
    if(nullptr != dynamic_cast<TypeInteger*>(type))
        os << "\"type\": \"integer\"";

    if(nullptr != dynamic_cast<TypeBoolean*>(type))
        os << "\"type\": \"boolean\"";
        
    if(nullptr != dynamic_cast<TypeNumber*>(type))
        os << "\"type\": \"number\"";

    if(nullptr != dynamic_cast<TypeString*>(type))
        os << "\"type\": \"string\"";

    if(auto array = dynamic_cast<TypeArray*>(type))
    {
        os << "\"type\": \"array\", \"body\": {\"size\": "<< array->size << ", ";
        encode(os, array->base, prefix + "    ");
        os << "}";
    }

    if(auto record = dynamic_cast<TypeRecord*>(type))
    {
        os << "\"type\": \"record\", \"body\": [";

        auto    suffix   = "\n";
        for(auto& item: record->body())
        {
            os << suffix << prefix << "    {\"name\": \"" << item.name << "\", ";
            encode(os, item.type, prefix + "    ");
            os << "}";

            suffix = ",\n";
        }
        os << "]";
    }
}

std::string Writer::encode( Type*   type)
{
    std::ostringstream  os;
    os << "{";
    referee::db::encode(os, type);
    os << "}";

    return os.str();
}
#define     INFO(type, ID)  (((type) << 16) | (ID))
#define     ROOT        0x0001
#define     DECL_TYPE   0x0002
#define     DECL_PROP   0x0003
#define     DECL_CONF   0x0004
#define     PUSH_PROP   0x0005
#define     PUSH_CONF   0x0006

uint8_t Writer::declType(   Type*               type)
{
    std::string encoded = encode(type);

    m_types.push_back(type);

    record(INFO(DECL_TYPE, 0), encoded);

    return  m_types.size();
}

uint8_t Writer::declConf(   uint8_t             type,
                            std::string         name)
{
    m_confs.push_back(std::make_pair(name, type));

    record(INFO(DECL_CONF, type), name);

    return m_confs.size();
}

void    Writer::pushData(   uint8_t             conf,
                            std::string const&  data)
{
    record(INFO(PUSH_CONF, conf), data);
}
                    
uint8_t Writer::declProp(   uint8_t             type,
                            std::string         name)
{
    m_props.push_back(std::make_pair(name, type));

    record(INFO(DECL_PROP, type), name);

    return m_props.size();
}

void    Writer::pushData(   uint8_t             prop,
                            uint64_t            time,
                            std::string const&  data)
{
    record(INFO(PUSH_PROP, prop), time, data);
}

void    readDB(std::string filename)
{
    std::ifstream   is(filename, std::ios_base::binary | std::ios_base::in);

    std::vector<std::string>    prop2name;
    std::vector<std::string>    conf2name;

    conf2name.push_back("-");
    prop2name.push_back("-");

    while(is.eof() == false)
    {
        uint16_t    type;
        uint32_t    info;
        uint32_t    size;
        uint64_t    time;
        uint8_t     indx;
        std::string data;
        is.read(reinterpret_cast<char*>(&info), sizeof(info));
        is.read(reinterpret_cast<char*>(&size), sizeof(size));

        info    = ntohl(info);
        size    = ntohl(size);
        type    = info >> 16;
        indx    = info & 0xff;


        switch(type)
        {
            case ROOT:
                data.resize(size);
                is.read(reinterpret_cast<char*>(data.data()), size);
                std::cout << data << std::endl;
                break;
            case DECL_TYPE:
                data.resize(size);
                is.read(reinterpret_cast<char*>(data.data()), size);
                std::cout << "decl type: "  << data << std::endl;
                break;
            case DECL_PROP:
                data.resize(size);
                is.read(reinterpret_cast<char*>(data.data()), data.size());
                std::cout << "decl prop: " << data << std::endl;
                prop2name.push_back(data);
                break;
            case DECL_CONF:
                data.resize(size);
                is.read(reinterpret_cast<char*>(data.data()), size);
                std::cout << "decl conf: "  << data << std::endl;
                conf2name.push_back(data);
                break;
            case PUSH_PROP:
                data.resize(size - sizeof(time));
                is.read(reinterpret_cast<char*>(&time), sizeof(time));
                time    = ntohll(time);
                is.read(reinterpret_cast<char*>(data.data()), data.size());
                std::cout << prop2name[indx] << " @ " << std::dec << std::setw(16) << std::setfill('0') << time << ":";
                printHex(data) << std::endl;
                break;
            case PUSH_CONF:
                data.resize(size);
                is.read(reinterpret_cast<char*>(data.data()), data.size());
                std::cout << prop2name[indx];
                printHex(data) << std::endl;
                break;
            default:
                std::exit(1);
        }

    }
}

}
