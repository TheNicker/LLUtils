/*
Copyright (c) 2019 Lior Lahav

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once
#include "Exception.h"
#include <cassert>
#include <vector>
#include <set>
namespace LLUtils
{
    template <typename T, typename Container = std::set<T>>
    class UniqueIdProvider
    {
	public:
		using underlying_type = T;
    private:
        //Note: Do not change the order of the members deceleration
        // if its inevitable keep 'fFreeIds' prior to 'fFreeIdsEnd'
        Container fFreeIds;
        typename Container::const_iterator fFreeIdsEnd;
        T fStartID;
        T fNextId;


    public:
        UniqueIdProvider(const T startID = 0) : fFreeIdsEnd(fFreeIds.end()) , fStartID(startID) , fNextId(startID)
        {

        }

        void Reset()
        {
            fNextId = fStartID;
            fFreeIds.clear();
        }

        underlying_type GetNextID() const
        {
            return fNextId;
        }


        underlying_type GetStartID() const
        {
            return fStartID;
        }

        

        const T Acquire()
        {
            T result;
            if (fFreeIds.empty())
            {
                result = fNextId++;
            }
            else
            {
                typename Container::iterator it = fFreeIds.begin();
                result = *it;
                fFreeIds.erase(it);
            }
            return result;
        }

        static constexpr bool IsVector = std::is_same_v<Container, typename std::vector<T>>;
        
        /*
        template <typename = typename std::enable_if_t<IsVector>>
        void Release(const T id)
        {
            ThrowException("Trying to release an id that has never been acquired", id < fNextId && id >= fStartID);
            assert("id already released" &&  (std::find(fFreeIds.begin(), fFreeIds.end(),id) == fFreeIds.end())    );
            fFreeIds.push_back(id);
        }
        */

        template <typename = typename std::enable_if_t<!IsVector>>
        void Release(const T id)
        {

        #ifdef DEBUG
            if (!(id < fNextId && id >= fStartID))
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::LogicError, "Trying to release an id that has never been acquired");
        #endif

            auto result = fFreeIds.insert(id);

            if (result.second == false)
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::LogicError, "id already released");
        }
        
        void Normalize()
        {
            typename Container::const_iterator it;
            while ((it = fFreeIds.find(fNextId - 1)) != fFreeIdsEnd)
            {
                fFreeIds.erase(it);
                fNextId--;
            }
        }
    };
}