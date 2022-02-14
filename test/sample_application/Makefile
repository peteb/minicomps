CXX = time -f "%e" clang++
CXXFLAGS = -std=c++17 -fno-exceptions -fvisibility-inlines-hidden -fno-rtti -fno-threadsafe-statics -I. -I../../tools/ -I../../include/ -I../../minicoros/include/ -O0

core_files = ../../src/component.o ../../src/executor.o ../../src/broker.o ../../tools/testing.o

obj_files = $(core_files) test_session_system.o user/user_system_impl.o orchestration/composition_root.o session/session_system_impl.o \
	session/session.o component_types.o session/session_system.o session/session_system_fake.o

%.o: %.cc
	$(CXX) -c $(CXXFLAGS) $< -o $@

test: $(obj_files)
	$(CXX) $(obj_files) -lpthread

clean:
	rm *.o
