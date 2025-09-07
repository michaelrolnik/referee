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

#include "gtest/gtest.h"
#include "visitors/canonic.hpp"
#include "factory.hpp"
#include <iostream>

class testCanonic
    : public ::testing::Test
{
public:
    void    SetUp()
    {
        t   = Factory<ExprConstBoolean>::create(true);
        f   = Factory<ExprConstBoolean>::create(false);
    }

    Expr*   t = nullptr;
    Expr*   f = nullptr;
};


TEST_F(testCanonic, True)
{
    auto    inp = t;
    auto    exp = t;
    EXPECT_EQ(Canonic::make(inp), exp);
}

TEST_F(testCanonic, NotTrue)
{
    auto    inp = Factory<ExprNot>::create(t);
    auto    exp = f;
    EXPECT_EQ(Canonic::make(inp), exp);
}

TEST_F(testCanonic, False)
{
    auto    inp = f;
    auto    exp = f;
    EXPECT_EQ(Canonic::make(inp), exp);
}

TEST_F(testCanonic, NotFalse)
{
    auto    inp = Factory<ExprNot>::create(f);
    auto    exp = t;
    EXPECT_EQ(Canonic::make(inp), exp);
}

TEST_F(testCanonic, G)
{
    Time*   time= nullptr;
    auto    inp = Factory<ExprG>::create(time, t);
    auto    exp = Factory<ExprRw>::create(time, f, t);
    EXPECT_EQ(Canonic::make(inp), exp);
}

TEST_F(testCanonic, F)
{
    Time*   time= nullptr;
    auto    inp = Factory<ExprF>::create(time, f);
    auto    exp = Factory<ExprUs>::create(time, t, f);
    EXPECT_EQ(Canonic::make(inp), exp);
}
