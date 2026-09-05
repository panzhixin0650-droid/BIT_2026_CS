#pragma once

#include "i_repository.h"

namespace charging::server {

// Every early return rolls back all order, pile and balance changes.
class RepositoryTransaction final {
public:
    explicit RepositoryTransaction(IRepository *repository)
        : repository_(repository)
        , active_(repository != nullptr && repository->beginTransaction())
    {
    }

    ~RepositoryTransaction()
    {
        if (active_) repository_->rollbackTransaction();
    }

    RepositoryTransaction(const RepositoryTransaction &) = delete;
    RepositoryTransaction &operator=(const RepositoryTransaction &) = delete;

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] bool commit()
    {
        if (!active_ || !repository_->commitTransaction()) return false;
        active_ = false;
        return true;
    }

private:
    IRepository *repository_;
    bool active_;
};

}  // namespace charging::server
