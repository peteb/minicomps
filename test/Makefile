CXX = clang++
CXXFLAGS = -std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -I../include/ -I../tools/ -I../minicoros/include/ -O3

core_files = ../src/component.o ../src/executor.o ../src/broker.o ../tools/testing.o
core_tests = test_fixed_any.o test_broker.o  test_coroutine_query.o test_event.o test_sync_query.o test_async_query.o test_async_query_filter.o test_interface_async.o
perf_tests = test_event_perf.o test_async_query_perf.o test_sync_query_perf.o
example_tests = test_example_subsessions.o test_example_request_coalescing.o test_example_dep_verification.o
obj_files = $(core_files) $(core_tests) $(perf_tests) $(example_tests)

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
