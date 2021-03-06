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
#include <chrono>

namespace LLUtils
{
    template <typename time_type_real64 = long double
    , typename time_type_integer64 = int64_t
    , typename base_clock = std::chrono::high_resolution_clock>
    
    
    class StopWatchBasic
    {
    public:
        enum TimeUnit { Undefined = 0, NanoSeconds = 1, MicroSeconds = 2, Milliseconds = 3, Seconds = 4, TimeUnitCount};
        using time_type_integer = time_type_integer64;
        using time_type_real = time_type_real64;

    private:
        using base_clock_rep = typename base_clock::rep;
        using base_clock_time_point = typename base_clock::time_point;
        
        static constexpr time_type_real multiplierMapReal      [TimeUnitCount] = { 0.0, 1.0 , 0.001 , 0.000001 ,0.000000001};
        static constexpr base_clock_rep multiplierMapInteger   [TimeUnitCount] = { 0, 1 , 1000  , 1000000  ,1000000000 };
        
    public:
        StopWatchBasic(bool start = false)
        {
            if (start)
                Start();
        }

        void Start()
        {
            fStart = base_clock::now();
            fIsRunning = true;
        }
        void Stop()
        {
            fIsRunning = false;
            fEnd = base_clock::now();
        }

        time_type_integer GetElapsedTimeInteger(TimeUnit timeUnit) const
        {
            return static_cast<time_type_integer>(GetElapsedNanoSeconds() / multiplierMapInteger[timeUnit]);
        }

        time_type_real GetElapsedTimeReal(TimeUnit timeUnit) const
        {
            return static_cast<time_type_real>(GetElapsedNanoSeconds()) * multiplierMapReal [timeUnit];
        }
    private:

        base_clock_rep GetElapsedNanoSeconds() const
        {
            
            base_clock_time_point end = fIsRunning ? base_clock::now() : fEnd;
            return (end - fStart).count();
        }

    private:
        base_clock_time_point fStart;
        base_clock_time_point fEnd;
        bool fIsRunning = false;
    };

    typedef StopWatchBasic<> StopWatch;
}
