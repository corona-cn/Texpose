#pragma once
#include <cassert>

#include "ThreadExposee.hpp"
#include "../../../src/texpose/expose/ThreadExposer.hpp"
#include "../../../src/texpose/expose/ThreadExposee.hpp"

namespace Texpose::include::expose {
    class ThreadExposer {
        public:
            class Iterator {
                public:
                    explicit Iterator(src::expose::ThreadExposee** inBeginThreadExposees = nullptr, src::expose::ThreadExposee** inEndThreadExposees = nullptr) {
                        this->beginThreadExposees = inBeginThreadExposees;
                        this->endThreadExposees = inEndThreadExposees;
                    }
                    ~Iterator() = default;
                    Iterator(const Iterator&) = delete;
                    Iterator& operator = (const Iterator&) = delete;
                    Iterator(Iterator&& inOther) noexcept {
                        this->beginThreadExposees = inOther.beginThreadExposees;
                        this->endThreadExposees = inOther.endThreadExposees;

                        inOther.beginThreadExposees = nullptr;
                        inOther.endThreadExposees = nullptr;
                    }
                    Iterator& operator = (Iterator&& inOther) noexcept {
                        if (this != &inOther) {
                            this->beginThreadExposees = inOther.beginThreadExposees;
                            this->endThreadExposees = inOther.endThreadExposees;

                            inOther.beginThreadExposees = nullptr;
                            inOther.endThreadExposees = nullptr;
                        }

                        return *this;
                    }

                    auto operator * () const -> ThreadExposee {
                        return ThreadExposee(*this->beginThreadExposees);
                    }

                    auto operator ++ () -> Iterator& {
                        ++this->beginThreadExposees;
                        return *this;
                    }
                    auto operator ++ (int) -> Iterator {
                        auto previousIterator = Iterator(this->beginThreadExposees, this->endThreadExposees);
                        ++this->beginThreadExposees;
                        return previousIterator;
                    }

                    auto operator == (const Iterator& inOther) const -> bool {
                        if (this->beginThreadExposees == this->endThreadExposees || inOther.beginThreadExposees == inOther.endThreadExposees) {
                            return this->beginThreadExposees == this->endThreadExposees && inOther.beginThreadExposees == inOther.endThreadExposees;
                        }

                        return this->beginThreadExposees == inOther.beginThreadExposees;
                    }
                    auto operator != (const Iterator& inOther) const -> bool {
                        return !(*this == inOther);
                    }

                private:
                    src::expose::ThreadExposee** beginThreadExposees;
                    src::expose::ThreadExposee** endThreadExposees;
            };

            class ConstIterator {
                public:
                    explicit ConstIterator(src::expose::ThreadExposee* const* inBeginThreadExposees = nullptr, src::expose::ThreadExposee* const* inEndThreadExposees = nullptr) {
                        this->beginThreadExposees = inBeginThreadExposees;
                        this->endThreadExposees = inEndThreadExposees;
                    }
                    ~ConstIterator() = default;
                    ConstIterator(const ConstIterator&) = delete;
                    ConstIterator& operator = (const ConstIterator&) = delete;
                    ConstIterator(ConstIterator&& inOther) noexcept {
                        this->beginThreadExposees = inOther.beginThreadExposees;
                        this->endThreadExposees = inOther.endThreadExposees;

                        inOther.beginThreadExposees = nullptr;
                        inOther.endThreadExposees = nullptr;
                    }
                    ConstIterator& operator = (ConstIterator&& inOther) noexcept {
                        if (this != &inOther) {
                            this->beginThreadExposees = inOther.beginThreadExposees;
                            this->endThreadExposees = inOther.endThreadExposees;

                            inOther.beginThreadExposees = nullptr;
                            inOther.endThreadExposees = nullptr;
                        }

                        return *this;
                    }

                    auto operator * () const -> ThreadExposee {
                        return ThreadExposee((src::expose::ThreadExposee*) (*this->beginThreadExposees));
                    }

                    auto operator ++ () -> ConstIterator& {
                        ++this->beginThreadExposees;
                        return *this;
                    }
                    auto operator ++ (int) -> ConstIterator {
                        auto previousConstIterator = ConstIterator(this->beginThreadExposees, this->endThreadExposees);
                        ++this->beginThreadExposees;
                        return previousConstIterator;
                    }

                    auto operator == (const ConstIterator& inOther) const -> bool {
                        if (this->beginThreadExposees == this->endThreadExposees || inOther.beginThreadExposees == inOther.endThreadExposees) {
                            return this->beginThreadExposees == this->endThreadExposees && inOther.beginThreadExposees == inOther.endThreadExposees;
                        }

                        return this->beginThreadExposees == inOther.beginThreadExposees;
                    }
                    auto operator != (const ConstIterator& inOther) const -> bool {
                        return !(*this == inOther);
                    }

                private:
                    src::expose::ThreadExposee* const* beginThreadExposees;
                    src::expose::ThreadExposee* const* endThreadExposees;
            };

            auto isValid() const -> bool {
                return this->handle != nullptr;
            }

            static auto current() -> ThreadExposer {
                return ThreadExposer(src::expose::ThreadExposer_current());
            }

            explicit ThreadExposer(src::expose::ThreadExposer* inHandle = nullptr) {
                this->handle = inHandle;
            }
            ~ThreadExposer() = default;
            ThreadExposer(const ThreadExposer&) = delete;
            ThreadExposer& operator = (const ThreadExposer&) = delete;
            ThreadExposer(ThreadExposer&& inOther) noexcept {
                this->handle = inOther.handle;

                inOther.handle = nullptr;
            }
            ThreadExposer& operator = (ThreadExposer&& inOther) noexcept {
                if (this != &inOther) {
                    this->handle = inOther.handle;

                    inOther.handle = nullptr;
                }

                return *this;
            }

            auto expose(
                void** inExposeePtr, usize* inExposeeSizePtr,
                const void* inThreadExposeeRelativeIdentity,
                void (*inThreadExitCallback)(void* inExposee, void* inExternal) = nullptr,
                void (*inDestroyExposeeCallback)(void* inExposee, void* inExternal) = nullptr,
                void* inExternal = nullptr
            ) -> ThreadExposee {
                assert(this->isValid());
                return ThreadExposee(src::expose::ThreadExposer_expose(this->handle, inExposeePtr, inExposeeSizePtr, inThreadExposeeRelativeIdentity, inThreadExitCallback, inDestroyExposeeCallback, inExternal));
            }
            auto unexpose(ThreadExposee& inThreadExposee) -> void {
                assert(this->isValid());
                if (inThreadExposee.rawHandle()) {
                    assert(inThreadExposee.rawHandle()->ownerThreadExposer == this->handle);
                    if (inThreadExposee.rawHandle()->ownerThreadExposer == this->handle) {
                        src::expose::ThreadExposer_unexpose(this->handle, inThreadExposee.rawHandle());

                        inThreadExposee.rawHandle() = nullptr;
                    }
                }
            }

            auto begin() -> Iterator {
                assert(this->isValid());

                auto* const beginThreadExposees = src::expose::ThreadExposer_begin(this->handle);
                auto* const endThreadExposees = src::expose::ThreadExposer_end(this->handle);

                return Iterator(beginThreadExposees, endThreadExposees);
            }
            auto begin() const -> ConstIterator {
                assert(this->isValid());

                const auto* const beginThreadExposees = src::expose::ThreadExposer_begin(this->handle);
                const auto* const endThreadExposees = src::expose::ThreadExposer_end(this->handle);

                return ConstIterator(beginThreadExposees, endThreadExposees);
            }
            auto end() -> Iterator {
                assert(this->isValid());

                auto* const endThreadExposees = src::expose::ThreadExposer_end(this->handle);

                return Iterator(endThreadExposees, endThreadExposees);
            }
            auto end() const -> ConstIterator {
                assert(this->isValid());

                const auto* const endThreadExposees = src::expose::ThreadExposer_end(this->handle);

                return ConstIterator(endThreadExposees, endThreadExposees);
            }

            auto find(const void* inThreadExposeeRelativeIdentity) const -> ThreadExposee {
                assert(this->isValid());
                return ThreadExposee(src::expose::ThreadExposer_find(this->handle, inThreadExposeeRelativeIdentity));
            }

        private:
            src::expose::ThreadExposer* handle = nullptr;
    };
}