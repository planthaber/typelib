#include <boost/test/auto_unit_test.hpp>

#include <test/testsuite.hh>
#include <utilmm/configfile/configset.hh>
#include <typelib/pluginmanager.hh>
#include <typelib/importer.hh>
#include <typelib/typemodel.hh>
#include <typelib/registry.hh>
#include <typelib/value.hh>
#include <typelib/value_ops.hh>

#include <test/test_cimport.1>
#include <string.h>

using namespace Typelib;
using namespace std;

BOOST_AUTO_TEST_CASE( test_marshalling_simple )
{
    // Get the test file into repository
    Registry registry;
    PluginManager::self manager;
    Importer* importer = manager->importer("c");
    utilmm::config_set config;
    BOOST_REQUIRE_NO_THROW( importer->load(TEST_DATA_PATH("test_cimport.1"), config, registry) );

    /* Check a simple structure which translates into MEMCPY */
    {
        Type const& type = *registry.get("/struct A");
        A a;
        memset(&a, 1, sizeof(A));
        a.a = 10000;
        a.b = 1000;
        a.c = 100;
        a.d = 10;
        vector<uint8_t> buffer = dump(Value(&a, type));

        BOOST_REQUIRE_EQUAL( buffer.size(), sizeof(a));
        BOOST_REQUIRE( !memcmp(&buffer[0], &a, sizeof(a)) );

        A reloaded;
        load(Value(&reloaded, type), buffer);
        BOOST_REQUIRE( !memcmp(&reloaded, &a, sizeof(a)) );

        // Try (in order)
        //  - smaller type
        //  - bigger type
        //  - bigger buffer
        //  - smaller buffer
        BOOST_REQUIRE_THROW(load(Value(&reloaded, *registry.build("/int[200]")), buffer), std::runtime_error);
        BOOST_REQUIRE_THROW(load(Value(&reloaded, *registry.get("/int")), buffer), std::runtime_error);
        buffer.resize(buffer.size() + 2);
        BOOST_REQUIRE_THROW(load(Value(&reloaded, type), buffer), std::runtime_error);
        buffer.resize(buffer.size() - 4);
        BOOST_REQUIRE_THROW(load(Value(&reloaded, type), buffer), std::runtime_error);
    }

    /* Now, insert SKIPS into it */
    {
        A a;
        int align1 = offsetof(A, b) - sizeof(a.a);
        int align2 = offsetof(A, c) - sizeof(a.b) - offsetof(A, b);
        int align3 = offsetof(A, d) - sizeof(a.c) - offsetof(A, c);
        int align4 = sizeof(A)      - sizeof(a.d) - offsetof(A, d);
        size_t raw_ops[] = {
            MemLayout::FLAG_MEMCPY, sizeof(long long),
            MemLayout::FLAG_SKIP, align1,
            MemLayout::FLAG_MEMCPY, sizeof(int),
            MemLayout::FLAG_SKIP, align2,
            MemLayout::FLAG_MEMCPY, sizeof(char),
            MemLayout::FLAG_SKIP, align3,
            MemLayout::FLAG_MEMCPY, sizeof(short)
        };

        MemoryLayout ops;
        ops.insert(ops.end(), raw_ops, raw_ops + 14);

        Type const& type = *registry.get("/struct A");
        memset(&a, 1, sizeof(A));
        a.a = 10000;
        a.b = 1000;
        a.c = 100;
        a.d = 10;
        vector<uint8_t> buffer;
        dump(Value(&a, type), buffer, ops);
        BOOST_REQUIRE_EQUAL( sizeof(A) - align1 - align2 - align3 - align4, buffer.size());
        BOOST_REQUIRE_EQUAL( *reinterpret_cast<long long*>(&buffer[0]), a.a );

        A reloaded;
        memset(&reloaded, 2, sizeof(A));
        load(Value(&reloaded, type), buffer, ops);
        BOOST_REQUIRE_EQUAL(-1, memcmp(&a, &reloaded, sizeof(A)));
        BOOST_REQUIRE_EQUAL(a.a, reloaded.a);
        BOOST_REQUIRE_EQUAL(a.b, reloaded.b);
        BOOST_REQUIRE_EQUAL(a.c, reloaded.c);
        BOOST_REQUIRE_EQUAL(a.d, reloaded.d);
    }

    // And now check the array semantics
    {
        B b;
        size_t raw_ops[] = {
            MemLayout::FLAG_MEMCPY, offsetof(B, c),
            MemLayout::FLAG_ARRAY, 100,
                MemLayout::FLAG_MEMCPY, sizeof(b.c[0]),
            MemLayout::FLAG_END,
            MemLayout::FLAG_MEMCPY, sizeof(B) - offsetof(B, d)
        };

        MemoryLayout ops;
        ops.insert(ops.end(), raw_ops, raw_ops + 9);

        Type const& type = *registry.get("/struct B");
        vector<uint8_t> buffer;
        dump(Value(&b, type), buffer, ops);
        BOOST_REQUIRE_EQUAL( sizeof(B), buffer.size());
        BOOST_REQUIRE(!memcmp(&buffer[0], &b, sizeof(B)));

        B reloaded;
        load(Value(&reloaded, type), buffer, ops);
        BOOST_REQUIRE(!memcmp(&b, &reloaded, sizeof(B)));
    }
}

BOOST_AUTO_TEST_CASE(test_marshalapply_containers)
{
    // Get the test file into repository
    Registry registry;
    PluginManager::self manager;
    Importer* importer = manager->importer("c");
    utilmm::config_set config;
    BOOST_REQUIRE_NO_THROW( importer->load(TEST_DATA_PATH("test_cimport.1"), config, registry) );

    {
        StdCollections data;
        data.iv = 10;
        data.dbl_vector.resize(5);
        for (int i = 0; i < 5; ++i)
            data.dbl_vector[i] = 0.01 * i;
        data.v8  = -106;
        data.v_of_v.resize(5);
        for (int i = 0; i < 5; ++i)
        {
            data.v_of_v[i].resize(3);
            for (int j = 0; j < 3; ++j)
                data.v_of_v[i][j] = i * 10 + j;
        }
        data.v16 = 5235;
        data.v64 = 5230971546;

        Type const& type       = *registry.get("/struct StdCollections");
        vector<uint8_t> buffer = dump(Value(&data, type));
        BOOST_REQUIRE_EQUAL( buffer.size(),
                sizeof(StdCollections) - sizeof(std::vector<double>) - sizeof (std::vector< std::vector<double> >)
                + sizeof(double) * 20 // elements
                + 7 * sizeof(uint64_t)); // element counts
        BOOST_REQUIRE(! memcmp(&data.iv, &buffer[0], sizeof(data.iv)));
        BOOST_REQUIRE_EQUAL(5, *reinterpret_cast<uint64_t*>(&buffer[8]));
        BOOST_REQUIRE(! memcmp(&data.dbl_vector[0], &buffer[16], sizeof(double) * 5));

        BOOST_REQUIRE_EQUAL(5, *reinterpret_cast<uint64_t*>(&buffer[64]));
        for (int i = 0; i < 5; ++i)
        {
            size_t base_offset = i * (sizeof(double) * 3 + sizeof(uint64_t));
            BOOST_REQUIRE_EQUAL(3, *reinterpret_cast<uint64_t*>(&buffer[72 + base_offset]));
            BOOST_REQUIRE(! memcmp(&data.v_of_v[i][0], &buffer[80 + base_offset], sizeof(double) * 3));
        }

        StdCollections reloaded;
        load(Value(&reloaded, type), buffer);
        BOOST_REQUIRE( data.iv         == reloaded.iv );
        BOOST_REQUIRE( data.dbl_vector == reloaded.dbl_vector );
        BOOST_REQUIRE( data.v8         == reloaded.v8 );
        BOOST_REQUIRE( data.v16        == reloaded.v16 );
        BOOST_REQUIRE( data.v64        == reloaded.v64 );
    }

    {
        StdCollections data;
        
        data.iv = 0;
        data.v8 = 1;
        data.v_of_v.resize(5);
        data.v16 = 2;
        data.v64 = 3;

        Type const& type       = *registry.get("/struct StdCollections");
        vector<uint8_t> buffer = dump(Value(&data, type));
        BOOST_REQUIRE_EQUAL( buffer.size(),
                sizeof(StdCollections) - sizeof(std::vector<double>) - sizeof (std::vector< std::vector<double> >)
                + 7 * sizeof(uint64_t)); // element counts
        BOOST_REQUIRE(! memcmp(&data.iv, &buffer[0], sizeof(data.iv)));
        BOOST_REQUIRE_EQUAL(0, *reinterpret_cast<uint64_t*>(&buffer[8]));
        BOOST_REQUIRE_EQUAL(5, *reinterpret_cast<uint64_t*>(&buffer[24]));
        for (int i = 0; i < 5; ++i)
        {
            size_t base_offset = i * sizeof(uint64_t);
            BOOST_REQUIRE_EQUAL(0, *reinterpret_cast<uint64_t*>(&buffer[32 + base_offset]));
        }
    }
}
