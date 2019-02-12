{protocol_docs}
template <typename D, typename Base = internal::base_mixin>
class {protocol_name}Protocol : public Base {{
public:
    {protocol_name}Protocol() {{
        internal::Check{protocol_name}ProtocolSubclass<D>();
{constructor_definition}
    }}

protected:
    {protocol_name_snake}_protocol_ops_t {protocol_name_snake}_protocol_ops_ = {{}};

private:
{protocol_definitions}
}};

class {protocol_name}ProtocolClient {{
public:
    {protocol_name}ProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {{}}
    {protocol_name}ProtocolClient(const {protocol_name_snake}_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {{}}

    {protocol_name}ProtocolClient(zx_device_t* parent) {{
        {protocol_name_snake}_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_{protocol_name_uppercase}, &proto) == ZX_OK) {{
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        }} else {{
            ops_ = nullptr;
            ctx_ = nullptr;
        }}
    }}

    void GetProto({protocol_name_snake}_protocol_t* proto) const {{
        proto->ctx = ctx_;
        proto->ops = ops_;
    }}
    bool is_valid() const {{
        return ops_ != nullptr;
    }}
    void clear() {{
        ctx_ = nullptr;
        ops_ = nullptr;
    }}

{client_definitions}
private:
    {protocol_name_snake}_protocol_ops_t* ops_;
    void* ctx_;
}};
