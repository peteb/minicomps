CXX = clang++
CXXFLAGS = -std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -I../include/ -I../tools/ -I../minicoros/include/ -O3
obj_files = ../tools/testing.o test_broker.o  ../src/broker.o test_sync_query.o ../src/component.o  test_async_query.o test_any_aligned_storage.o test_async_query_perf.o test_sync_query_perf.o

compile_duration_files = test_compile_duration.o
comparison_files = test_comparison.o

%.o: %.cc ../include/coro.h
	$(CXX) -c $(CXXFLAGS) $< -o $@

test: $(obj_files)
	$(CXX) $(obj_files) -lpthread

test_compile_duration: $(compile_duration_files)
	$(CXX) $(compile_duration_files)

comparison: $(comparison_files)
	$(CXX) $(comparison_files)

clean:
	rm *.o
