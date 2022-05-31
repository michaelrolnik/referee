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

#include <arpa/inet.h>

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
    for(unsigned i = 0; i < data.size(); i++)
    {
        if(i % 16 == 0)
            std::cout << std::hex << std::setw(4) << std::setfill('0') <<  i << ": ";

        std::cout << std::hex << std::setw(2) << std::setfill('0') << (0xff & (unsigned)data[i]) << " ";
        if((i + 1) % 16 == 0)
            std::cout << std::endl;
    }

    if(data.size() % 16 != 0)
        std::cout << std::endl;

    return std::cout;
}

DataBuilder::DataBuilder(Type* type)
    : m_main(type)
{
    m_type.push_back(type);
}

template<typename Type>
Type*           DataBuilder::pop_type()
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

    return  type;
}

DataBuilder&    DataBuilder::integer(    int64_t             data)
{
    pop_type<TypeInteger>();

    auto    buff    = htonll(data);
    m_os.write(reinterpret_cast<char const*>(&buff), sizeof(buff));

    return  *this;
}

DataBuilder&    DataBuilder::number(     double              data)
{
    pop_type<TypeNumber>();

    auto    buff    = htonll(*reinterpret_cast<uint64_t*>(&data));
    m_os.write(reinterpret_cast<char const*>(&buff), sizeof(buff));

    return  *this;
}

DataBuilder&    DataBuilder::boolean(    bool                data)
{
    pop_type<TypeBoolean>();

    m_os.write(reinterpret_cast<char const*>(&data), sizeof(data));

    return  *this;
}

DataBuilder&    DataBuilder::string(     std::string const&  data)
{
    pop_type<TypeString>();

    auto    size    = htonl(data.size());
    m_os.write(reinterpret_cast<char const*>(&size), sizeof(size));
    m_os.write(reinterpret_cast<char const*>(data.data()), data.size());
    
    return *this;
}

DataBuilder&    DataBuilder::size(       unsigned            size)
{
    auto*   type    = pop_type<TypeArray>();

    if(type->size != 0 && type->size != size)
        throw   std::runtime_error("invalid array size");

    auto    buff    = htonl(size);
    m_os.write(reinterpret_cast<char const*>(&buff), sizeof(buff));

    for(auto i = 0; i < size; i++)
    {
        m_type.push_front(type->base);
    }

    return *this;
}

std::string     DataBuilder::build()
{
    return  m_os.str();
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

        if(auto curr = dynamic_cast<TypeInteger*>(top))
        {
            uint64_t    buff;

            is.read(reinterpret_cast<char*>(&buff), sizeof(buff));

            buff    = ntohll(buff);
            std::cout << std::dec << buff << " ";
        }

        if(auto curr = dynamic_cast<TypeNumber*>(top))
        {
            uint64_t    buff;

            is.read(reinterpret_cast<char*>(&buff), sizeof(buff));

            buff    = ntohll(buff);
            std::cout << std::dec << *reinterpret_cast<double*>(&buff) << " ";
        }    

        if(auto curr = dynamic_cast<TypeBoolean*>(top))
        {
            bool    buff;

            is.read(reinterpret_cast<char*>(&buff), sizeof(buff));

            std::cout << buff << " ";
        }    

        if(auto curr = dynamic_cast<TypeString*>(top))
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

            for(auto i = 0; i < size; i++)
            {
                type.push_front(curr->base);
            }
        }
    }
}

int main(int argc, char* argv[])
{
    try 
    {
        auto    type    = 
            TypeBuilderRecord()
                .integer("i")
                .number("n")
                .string("s")
                .array("xyz", 
                    TypeBuilderArray()
                        .integer()
                        .size(5)
                        .build())
                .record("rec", 
                    TypeBuilderRecord()
                        .boolean("b")
                        .integer("c")
                        .record("what", 
                            TypeBuilderRecord()
                                .integer("X")
                                .build())
                        .build())
                .build();
        print(type);

        std::string     data    = 
            DataBuilder(type)
                .integer(0xaaaaaaaa55555555)
                .number(0.5)
                .string("hello")
                .size(5)
                    .integer(1)
                    .integer(2)
                    .integer(3)
                    .integer(4)
                    .integer(5)
                .boolean(true)
                .integer(11111111)
                    .integer(0x777)
            .build();

        printHex(data) << std::endl;

        readData(type, data);
    }
    catch(std::exception& ex)
    {
        std::cerr << std::endl << "exception: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
