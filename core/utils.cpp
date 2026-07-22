/*
 *  MIT License
 *  
 *  Copyright (c) 2022-2026 Michael Rolnik
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

#include "utils.hpp"

#include <cmath>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <limits.h>

int64_t     parse_integer(  std::string const& text, unsigned base)
{
    int64_t res;
    char*   err     = nullptr;

    res = strtoll(text.c_str(), &err, base);

    if(     (err != nullptr && *err != 0) 
        ||  (res == LLONG_MAX) 
        ||  (res == LLONG_MIN))
    {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw std::runtime_error("not an integer, or out of range: '" + text + "'");
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }

    return  res;
}

int64_t     parse_binint(   std::string const& text) { return parse_integer(text,  2);}
int64_t     parse_octint(   std::string const& text) { return parse_integer(text,  8);}
int64_t     parse_decint(   std::string const& text) { return parse_integer(text, 10);}
int64_t     parse_hexint(   std::string const& text) { return parse_integer(text, 16);}

double      parse_number(   std::string const& text)
{
    double  res;
    char*   err     = nullptr;

    res = strtod(text.c_str(), &err);

    if(     (err != nullptr && *err != 0)  
        ||  (res == HUGE_VAL))
    {
//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
        throw std::runtime_error("not a number, or out of range: '" + text + "'");
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
    }

    return  res;
}

std::string parse_string(   std::string const& text)
{
    return text.substr(1, text.length() - 2);
}

bool        parse_boolean(  std::string const& text)
{
    if(text == "true")
        return true;

    if(text == "false")
        return false;

//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
    throw std::runtime_error("not a boolean, expected true or false: '" + text + "'");
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
}

//  LCOV_EXCL_START 
//  GCOV_EXCL_START 
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
//  GCOV_EXCL_STOP
//  LCOV_EXCL_STOP
