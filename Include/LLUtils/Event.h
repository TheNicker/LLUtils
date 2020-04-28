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
#include <functional>
#include <vector>
#include <algorithm>
namespace LLUtils
{

    template<typename T, typename... U>
    void* getAddress(std::function<T(U...)> f) {
        typedef T(fnType)(U...);
        fnType ** fnPointer = f.template target<fnType*>();
        return fnPointer != nullptr ? reinterpret_cast<void*>(*fnPointer) : nullptr;
    }

	template <class T>
	class Event
	{
	public:
		using Func = std::function<T>;

		template <class ...Args>
		void Raise(Args... args)
		{
			for (const auto& f : fListeners)
				f(args...);
		}
		
		void Add(const Func& func)
		{
			fListeners.push_back(func);
		}

		void Remove(const Func& func)
		{
            fListeners.erase(std::remove_if(fListeners.begin(), fListeners.end(), [&](const Func& elem)
            {
                return getAddress(func) == getAddress(elem);
            }));
		}

    private:
		using Listeners = std::vector<Func>;
		Listeners fListeners;
	};
}