add_executable(transaction_tracker_test transaction_tracker_test.cpp)

target_link_libraries( transaction_tracker_test fc ${Boost_LIBRARIES} ${PLATFORM_SPECIFIC_LIBS} )
target_compile_options(transaction_tracker_test PUBLIC -DDISABLE_EOSLIB_SERIALIZE)

add_test(NAME transaction_tracker
         COMMAND transaction_tracker_test)
