#include <ESPressio_StateVersion.hpp>
#include <cassert>
using namespace ESPressio::State;
int main(){
    auto value=NextStateVersion({},false);
    assert((value==StateVersion{false,1}));
    value={false,UINT16_MAX};
    value=NextStateVersion(value,true);
    assert((value==StateVersion{true,0}));
    assert(CompareStateVersion({false,10},{false,10})==StateVersionRelation::Duplicate);
    assert(CompareStateVersion({false,10},{false,11})==StateVersionRelation::Newer);
    assert(CompareStateVersion({false,10},{true,10})==StateVersionRelation::Ambiguous);
    assert(CompareStateVersion({false,11},{false,10})==StateVersionRelation::Older);
}
