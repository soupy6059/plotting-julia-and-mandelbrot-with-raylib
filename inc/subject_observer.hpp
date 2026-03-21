#pragma once

#include <memory>
#include <vector>
#include <any>

struct subject;

struct observer {
    virtual void handle(subject &From) = 0;
};

struct subject {
    std::vector<std::weak_ptr<observer>> Observers;
    enum signal {
        PRINT_ME,
    } Signal;
    struct status {
        signal Signal;
        std::any Load;
    } Status;
    subject &relay(status Status) {
        this->Status = std::move(Status); 
        for(uint64_t Obs = 0; Obs < Observers.size(); ++Obs) {
            if(Observers.at(Obs).expired()) {
                Observers.at(Obs) = std::move(*(Observers.end() - 1));
                Observers.pop_back();
                --Obs;
            }
            else Observers.at(Obs).lock()->handle(*this);
        }
        return *this;
    }
};

#ifdef 0
#include <string>
struct display: observer {
    virtual void handle(subject &From) override try {
        using namespace std;
        using enum subject::signal;
        switch(From.Status.Signal) {
        case PRINT_ME: {
            string to_print = any_cast<string>(From.Status.Load);
            cout << to_print << '\n';
        }
        default: break;
        }
    } catch(std::bad_cast &ex) {}
};
#endif
