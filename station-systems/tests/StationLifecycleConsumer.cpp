#include <station/StationLifecycleRuntime.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace station;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){require(std::abs(a-b)<1e-10,"Lifecycle clock mismatch");}
StationSystemsSettings settings(){StationSystemsSettings s;s.enabled=true;return s;}
const std::string spy=R"lua(
return function(name,...)
    assert(name=='Power' or name=='ECLSS' or name=='Thermal control' or name=='Monitoring/supervision')
    local n,elapsed=0,0
    return {advance=function(self,tick,dt,start)
        assert(tick==n+1,'step invoked twice or skipped')
        assert(math.abs(start-elapsed)<1e-10,'wrong variable clock')
        n=n+1;elapsed=elapsed+dt;return n
    end}
end
)lua";
void check(){
    StationLifecycleRuntime lifecycle(settings(),spy);
    require(lifecycle.completedSteps()==0,"Lifecycle started advanced");
    require(lifecycle.prepare(0.1) && lifecycle.prepare(0.1),"Prepared retry executed Lua twice");
    require(lifecycle.completedSteps()==0 && lifecycle.completedTicks()==std::array<uint64_t,4>{},"Prepared Lua published early");
    lifecycle.commit();near(lifecycle.elapsedSeconds(),0.1);
    for(double dt:{0.01,0.5,0.2,0.1}){require(lifecycle.prepare(dt),"Variable lifecycle duration failed");lifecycle.commit();}
    near(lifecycle.elapsedSeconds(),0.91);require(lifecycle.completedSteps()==5,"Wrong lifecycle cadence");
    require(lifecycle.prepare(0.3),"Candidate failed");
    require(!lifecycle.prepare(0.2,0) && !lifecycle.diagnostics().empty(),"Recovery work limit ignored");
    require(lifecycle.completedSteps()==5,"Refused recovery advanced time");
    require(lifecycle.prepare(0.2),"Different-duration reconstruction failed");lifecycle.commit();near(lifecycle.elapsedSeconds(),1.11);
    require(lifecycle.completedTicks()==std::array<uint64_t,4>{6,6,6,6},"Sandbox counters not committed together");
    require(!lifecycle.prepare(0) && lifecycle.completedSteps()==6,"Invalid duration changed lifecycle");
    // A failure after earlier sandboxes advanced must reconstruct all of them.
    StationLifecycleRuntime partial(settings(),R"lua(return function(name)
        local n=0
        return {advance=function(self,tick)
            assert(tick==n+1,'dirty earlier sandbox reused');n=n+1
            if name=='ECLSS' and tick==2 then error('intentional ECLSS failure') end
            return n
        end}
    end)lua");
    require(partial.prepare(0.1),"Initial step failed");partial.commit();
    for(int retry=0;retry<2;++retry){
        require(!partial.prepare(0.1),"Script failure ignored");
        require(partial.diagnostics()[0].system=="ECLSS" && partial.diagnostics()[0].message.find("intentional ECLSS")!=std::string::npos,"Failed sandbox reconstruction drifted");
        require(partial.completedSteps()==1 && partial.elapsedSeconds()==0.1,"Failed script committed partial state");
    }
    StationLifecycleRuntime bad(settings(),"return function() return {advance=function() return 0/0 end} end");
    require(!bad.prepare(0.1) && bad.diagnostics()[0].failure==ScriptExecutionFailure::ConversionError,"Invalid acknowledgement accepted");
    StationLifecycleRuntime runaway(settings(),"return function() return {advance=function() while true do end end} end");
    require(!runaway.prepare(0.1) && runaway.diagnostics()[0].failure==ScriptExecutionFailure::InstructionBudgetExceeded,"Lifecycle instruction limit missing");
    // Exercise the real bundled mirror with a schedule crossing mixed deltas.
    auto s=settings();s.stores={{"supply","kg",100,0,0}};
    s.deliveries={{"arrival","supply","kg",1,0.5,0,true,0}};
    auto scheduled=std::string("local factory=(function()\n")+bundledStationSystemsSource()+R"lua(
end)()
return function(...)
    local o=factory(...);local old=o.advance
    function o:advance(tick,dt,start)
        local completed=old(self,tick,dt,start)
        assert(self.stores[1].qualified==((start+dt>=0.5) and 1 or 0),'delivery used tick times latest delta')
        return completed
    end
    return o
end
)lua";
    StationLifecycleRuntime bundled(s,scheduled);
    for(double dt:{0.4,0.01,0.1}){require(bundled.prepare(dt),"Bundled lifecycle ignored supplied elapsed time");bundled.commit();}
    near(bundled.elapsedSeconds(),0.51);
}
}
int main(){try{check();std::cout<<"Station lifecycle passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
