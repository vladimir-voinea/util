// test_program.cpp : Defines the entry point for the console application.
//

#include <iostream>
#include "Concurrency.h"
using namespace std;
int main()
{
	int i = 0;
	conc::thread_pool tp;
	while (++i < 6000) {
		auto x = tp.async([&i] {
			return 1 + i;
		});
		auto y = tp.async([&i] {
			return 1 + i * 2;
		});
		auto z = tp.async([&i] {
			return 1 + i * 3;
		});
		auto a = tp.async([&i] {
			return 1 + i * 4;
		});
		cout << x.get() << y.get() << z.get() << a.get() << endl;
	}
	int n;
	cin >> n;
	
    return 0;
}

