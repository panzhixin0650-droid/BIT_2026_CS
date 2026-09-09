// 仓储事务小工具：未提交就离开作用域时自动回滚
#pragma once

#include "i_repository.h"

namespace charging::server {

// Every early return rolls back all order, pile and balance changes.
class RepositoryTransaction final {
public:
    // 构造时立即尝试开启事务，失败则 active 为假
    explicit RepositoryTransaction(IRepository *repository)
        : repository_(repository)
        , active_(repository != nullptr && repository->beginTransaction())
    {
    }

    // 析构时若未提交，自动回滚未完成的改动
    ~RepositoryTransaction()
    {
        if (active_) repository_->rollbackTransaction();
    }

    RepositoryTransaction(const RepositoryTransaction &) = delete;
    RepositoryTransaction &operator=(const RepositoryTransaction &) = delete;

    [[nodiscard]] bool active() const { return active_; }
    // 提交成功后清除标记，避免析构再次回滚
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
