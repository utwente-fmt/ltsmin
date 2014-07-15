#include <hre/config.h>

#include <assert.h>

#include <hre/runtime.h>
#include <util-lib/simplemap.h>

void simplemap_test()
{
    Print(infoLong, "Start of simplemap testsuite.");
    map_t map1 = simplemap_create(10);
    simplemap_put(map1, 5, 3);
    assert(simplemap_get(map1, 5) == 3);
    simplemap_put(map1, 2, 4);
    assert(simplemap_get(map1, 2) == 4);
    simplemap_put(map1, 1, 3);
    assert(simplemap_get(map1, 1) == 3);
    simplemap_put(map1, 200, 11);
    assert(simplemap_get(map1, 200) == 11);
    assert(simplemap_get(map1, 5) == 3);

    map_t map2 = simplemap_create(100);
    for(uint32_t i = 0; i < 100; i++)
    {
        simplemap_put(map2, i, 100-i);
        assert(simplemap_get(map2, i) == 100-i);
    }
    simplemap_destroy(map1);
    simplemap_destroy(map2);
    Print(infoLong, "End of simplemap testsuite.");
}

int main(int argc, char *argv[]){
    HREinitBegin(argv[0]);
    HREinitStart(&argc,&argv,0,-1,NULL,NULL);

    simplemap_test();
    HREexit(0);
}
