#include "preyvr/SceneQuery.h"
#include "preyvr/WeaponAim.h"
#include "preyvr/LatestSnapshot.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace preyvr;
void Check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
    scene::Params params;scene::Hit hit;int dummy=0;void* skip[]={&dummy};
    std::memset(&params,0xcd,sizeof(params));
    Check(scene::Build(params,{1,2,3},{0,1,0},hit,skip,1),"valid unit aim accepted");
    Check(params.origin.x==1&&params.delta.y==200&&params.objectTypes==0x11f&&params.flags==0xf,
        "native synchronous weapon-mask parameters");
    Check(params.hits==&hit&&params.capacity==1&&params.skip==skip&&params.skipCount==1,"caller-private hit and player skip");
    Check(!params.callback&&!params.foreignData&&!params.cached&&!params.trailing&&params.collisionIgnore==0x400000,
        "queued callbacks and cache disabled");
    Check(params.padding==0&&params.padding2==0&&params.padding3==0,"reserved bytes initialized");
    Check(!scene::Build(params,{},{0,2,0},hit,skip,1),"non-unit direction refused");
    Check(!scene::Build(params,{},{0,1,0},hit,nullptr,0),"self-filter required");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    Check(!scene::Build(params,{nan,0,0},{0,1,0},hit,skip,1),"invalid origin refused");
    hit.distance=3;Check(scene::Distance(1,hit)==3&&scene::Distance(0,hit)<0,"miss ignores old buffer");
    Check(scene::Distance(2,hit)<0,"impossible hit count refused");
    for(float invalid:{nan,-1.f,201.f}){hit.distance=invalid;Check(scene::Distance(1,hit)<0,"malformed distance refused");}
    hit.distance=0;Check(scene::Distance(1,hit)==0,"contact at origin valid");
    Check(scene::ReticleDistance(true,3,10)==3&&scene::ReticleDistance(false,3,10)==10&&
        scene::ReticleDistance(true,-1,10)==10,"enable and fallback policy");
    aim::Sample sample;sample.origin={1,2,3};sample.direction={0,1,0};sample.confidence=aim::Confidence::barrel;
    sample.sceneDistance=3;sample.equipGeneration=7;sample.referenceGeneration=2;sample.trackingSequence=8;sample.publishedNs=1000000;
    LatestSnapshot<aim::Sample> publication;publication.Publish(sample);aim::Sample read{};
    Check(publication.TryRead(read)&&read.sceneDistance==3&&read.trackingSequence==8,"hit travels with its ray");
    Check(aim::Usable(read,7,2,2000000)&&!aim::Usable(read,8,2,2000000)&&
        !aim::Usable(read,7,4,2000000)&&!aim::Usable(read,7,2,301000000),"equip reference and age reject hit with aim");
    sample.sceneDistance=-1;publication.Publish(sample);Check(publication.TryRead(read)&&read.sceneDistance<0,"new miss clears old hit");
    std::cout<<"Scene query ABI, private storage, fallback and aim provenance passed\n";
}
