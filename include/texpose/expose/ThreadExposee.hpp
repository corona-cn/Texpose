#pragma once
#include <assert.h>

#include "../../../src/texpose/expose/ThreadExposee.hpp"
#include "../../../src/texpose/expose/ThreadExposer.hpp"

namespace Texpose::include::expose {
    class ThreadExposee {
        public:
            auto isValid() const -> bool {
                return this->handle != nullptr;
            }

            explicit ThreadExposee(src::expose::ThreadExposee* inHandle = nullptr) {
                this->handle = inHandle;
            }
            ~ThreadExposee() = default;
            ThreadExposee(const ThreadExposee&) = delete;
            ThreadExposee& operator = (const ThreadExposee&) = delete;
            ThreadExposee(ThreadExposee&& inOther) noexcept {
                this->handle = inOther.handle;

                inOther.handle = nullptr;
            }
            ThreadExposee& operator = (ThreadExposee&& inOther) noexcept {
                if (this != &inOther) {
                    if (this->isValid()) {
                        assert(this->handle->ownerThreadExposer == src::expose::ThreadExposer_current());
                        if (this->handle->ownerThreadExposer == src::expose::ThreadExposer_current()) {
                            src::expose::ThreadExposer_unexpose(this->handle->ownerThreadExposer, this->handle);
                        }
                    }

                    this->handle = inOther.handle;

                    inOther.handle = nullptr;
                }

                return *this;
            }

            auto accessExposee() const -> void* {
                assert(this->isValid());
                return src::expose::ThreadExposee_accessExposee(this->handle);
            }
            auto peekExposee() const -> const void* {
                assert(this->isValid());
                return src::expose::ThreadExposee_peekExposee(this->handle);
            }
            auto peekExposeeSize() const -> usize {
                assert(this->isValid());
                return src::expose::ThreadExposee_peekExposeeSize(this->handle);
            }

        private:
            friend class ThreadExposer;

            src::expose::ThreadExposee* handle = nullptr;
    };
}