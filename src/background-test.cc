#include "background.h"
#include "gtest/gtest.h"

namespace yukino {

class BackgroundTest : public ::testing::Test {
public:

    virtual void SetUp() override {
        ASSERT_TRUE(background_ == nullptr);
        ASSERT_TRUE(queue_ == nullptr);

        background_ = new Background;
        queue_ = new Background::Delegate;
        background_->set_queue(queue_);
        background_->AsyncRun();
    }

    virtual void TearDown() override {
        queue_->PostShutdown();
        background_->WaitForShutdown();
        delete background_;
        background_ = nullptr;
        delete queue_;
        queue_ = nullptr;
    }

protected:
    Background *background_ = nullptr;
    BackgroundWorkQueue *queue_ = nullptr;

}; // class BackgroundTest

TEST_F(BackgroundTest, Sanity) {
    queue_->PostEcho(yuki::Slice("echo"));
}

} // namespace yukino