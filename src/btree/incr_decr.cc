#include "incr_decr.hpp"

#include "btree/modify_oper.hpp"

struct btree_incr_decr_oper_t : public btree_modify_oper_t {

    explicit btree_incr_decr_oper_t(bool increment, unsigned long long delta)
        : increment(increment), delta(delta)
    { }

    bool operate(transaction_t *txn, btree_value *old_value, large_buf_lock_t& old_large_buflock, btree_value **new_value, large_buf_lock_t& new_large_buflock) {
        // If the key didn't exist before, we fail
        if (!old_value) {
            result.res = store_t::incr_decr_result_t::idr_not_found;
            return false;
        }
        
        // If we can't parse the key as a number, we fail
        bool valid;
        unsigned long long number;
        if (old_value->size < 50) {
            char buffer[50];
            memcpy(buffer, old_value->value(), old_value->size);
            buffer[old_value->size] = 0;
            char *endptr;
            number = strtoull_strict(buffer, &endptr, 10);
            if (endptr == buffer) valid = false;
            else valid = true;
        } else {
            valid = false;
        }
        if (!valid) {
            result.res = store_t::incr_decr_result_t::idr_not_numeric;
            return false;
        }

        // If we overflow when doing an increment, set number to 0 (this is as memcached does it as of version 1.4.5)
        // for decrements, set to 0 on underflows
        if (increment) {
            if (number + delta < number) number = 0;
            else number += delta;
        } else {
            if (number - delta > number) number = 0;
            else number -= delta;
        }

        result.res = store_t::incr_decr_result_t::idr_success;
        result.new_value = number;

        // We write into our member variable 'temp_value' because the buffer we return must remain
        // valid until the btree FSM is destroyed. That's why we can't allocate a buffer on the
        // stack.

        valuecpy(&temp_value, old_value);
        int chars_written = sprintf(temp_value.value(), "%llu", (unsigned long long)number);
        rassert(chars_written <= MAX_IN_NODE_VALUE_SIZE); // Not really necessary.
        temp_value.value_size(chars_written);

        *new_value = &temp_value;
        return true;
    }

    bool increment;   // If false, then decrement
    unsigned long long delta;   // Amount to increment or decrement by

    /* Used as temporary storage, so that the value we return from operate() doesn't become invalid
    before run_btree_modify_oper is done with it. */
    union {
        char temp_value_memory[MAX_BTREE_VALUE_SIZE];
        btree_value temp_value;
    };

    store_t::incr_decr_result_t result;
};

store_t::incr_decr_result_t btree_incr_decr(const btree_key *key, btree_slice_t *slice, bool increment, unsigned long long delta, cas_t proposed_cas) {
    btree_incr_decr_oper_t oper(increment, delta);
    run_btree_modify_oper(&oper, slice, key, proposed_cas);
    return oper.result;
}