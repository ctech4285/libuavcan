/*
 * Copyright (C) 2014 Pavel Kirienko <pavel.kirienko@gmail.com>
 */

#pragma once

#include <cassert>
#include <sstream>
#include <uavcan/stdint.hpp>
#include <uavcan/impl_constants.hpp>
#include <uavcan/map.hpp>
#include <uavcan/debug.hpp>
#include <uavcan/transport/transfer.hpp>
#include <uavcan/time.hpp>

namespace uavcan
{

UAVCAN_PACKED_BEGIN
class OutgoingTransferRegistryKey
{
    DataTypeID data_type_id_;
    uint8_t transfer_type_;
    NodeID destination_node_id_;  ///< Not applicable for message broadcasting

public:
    OutgoingTransferRegistryKey()
    : transfer_type_(0xFF)
    { }

    OutgoingTransferRegistryKey(DataTypeID data_type_id, TransferType transfer_type, NodeID destination_node_id)
    : data_type_id_(data_type_id)
    , transfer_type_(transfer_type)
    , destination_node_id_(destination_node_id)
    {
        assert((transfer_type == TransferTypeMessageBroadcast) == destination_node_id.isBroadcast());

        /* Service response transfers must use the same Transfer ID as matching service request transfer,
         * so this registry is not applicable for service response transfers at all.
         */
        assert(transfer_type != TransferTypeServiceResponse);
    }

    bool operator==(const OutgoingTransferRegistryKey& rhs) const
    {
        return
            (data_type_id_        == rhs.data_type_id_) &&
            (transfer_type_       == rhs.transfer_type_) &&
            (destination_node_id_ == rhs.destination_node_id_);
    }

    std::string toString() const
    {
        std::ostringstream os;
        os << "dtid=" << int(data_type_id_.get())
            << " tt=" << int(transfer_type_)
            << " dnid=" << int(destination_node_id_.get());
        return os.str();
    }
};
UAVCAN_PACKED_END


class IOutgoingTransferRegistry
{
public:
    virtual ~IOutgoingTransferRegistry() { }
    virtual TransferID* accessOrCreate(const OutgoingTransferRegistryKey& key, MonotonicTime new_deadline) = 0;
    virtual void cleanup(MonotonicTime deadline) = 0;
};


template <int NumStaticEntries>
class OutgoingTransferRegistry : public IOutgoingTransferRegistry, Noncopyable
{
UAVCAN_PACKED_BEGIN
    struct Value
    {
        MonotonicTime deadline;
        TransferID tid;
    };
UAVCAN_PACKED_END

    class DeadlineExpiredPredicate
    {
        const MonotonicTime ts_;

    public:
        DeadlineExpiredPredicate(MonotonicTime ts)
        : ts_(ts)
        { }

        bool operator()(const OutgoingTransferRegistryKey& key, const Value& value) const
        {
            (void)key;
            assert(!value.deadline.isZero());
            const bool expired = value.deadline <= ts_;
            if (expired)
            {
                UAVCAN_TRACE("OutgoingTransferRegistry", "Expired %s tid=%i",
                             key.toString().c_str(), int(value.tid.get()));
            }
            return expired;
        }
    };

    Map<OutgoingTransferRegistryKey, Value, NumStaticEntries> map_;

public:
    OutgoingTransferRegistry(IAllocator& allocator)
    : map_(allocator)
    { }

    TransferID* accessOrCreate(const OutgoingTransferRegistryKey& key, MonotonicTime new_deadline)
    {
        assert(!new_deadline.isZero());
        Value* p = map_.access(key);
        if (p == NULL)
        {
            p = map_.insert(key, Value());
            if (p == NULL)
                return NULL;
            UAVCAN_TRACE("OutgoingTransferRegistry", "Created %s", key.toString().c_str());
        }
        p->deadline = new_deadline;
        return &p->tid;
    }

    void cleanup(MonotonicTime ts)
    {
        map_.removeWhere(DeadlineExpiredPredicate(ts));
    }
};

}