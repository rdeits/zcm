#include <iostream>
#include <sstream>

#include "Common.hpp"
#include "Emitter.hpp"
#include "GetOpt.hpp"
#include "ZCMGen.hpp"

#include "util/StringUtil.hpp"
#include "util/FileUtil.hpp"

using namespace std;

void setupOptionsJulia(GetOpt& gopt)
{
    gopt.addString(0, "julia-path", ".", "Julia destination directory");
}

static string dotsToSlashes(const string& s)
{
    return StringUtil::replace(s, '.', '/');
}

// Some types do not have a 1:1 mapping from zcm types to native Julia storage types.
static string mapTypeName(const string& t)
{
    if      (t == "int8_t")   return "Int8";
    else if (t == "int16_t")  return "Int16";
    else if (t == "int32_t")  return "Int32";
    else if (t == "int64_t")  return "Int64";
    else if (t == "byte")     return "UInt8";
    else if (t == "float")    return "Float32";
    else if (t == "double")   return "Float64";
    else if (t == "string")   return "String";
    else if (t == "boolean")  return "Bool";
    else {
        return t;
    }
}

struct EmitJulia : public Emitter
{
    ZCMGen& zcm;
    ZCMStruct& ls;

    EmitJulia(ZCMGen& zcm, ZCMStruct& ls, const string& fname):
        Emitter(fname), zcm(zcm), ls(ls) {}

    void emitAutoGeneratedWarning()
    {
        emit(0, "# THIS IS AN AUTOMATICALLY GENERATED FILE.  DO NOT MODIFY");
        emit(0, "# BY HAND!!");
        emit(0, "#");
        emit(0, "# Generated by zcm-gen");
        emit(0, "#");
        emit(0, "");
    }

    void emitComment(int indent, const string& comment)
    {
        if (comment == "")
            return;

        auto lines = StringUtil::split(comment, '\n');
        if (lines.size() == 1) {
            emit(indent, "# %s", lines[0].c_str());
        } else {
            for (auto& line : lines) {
                emitStart(indent, "#");
                if (line.size() > 0)
                    emitContinue("%s", line.c_str());
                emitEnd("");
            }
        }
    }

    void emitPackageNamespaceStart()
    {
        // output namespace declaration
        auto namespaces = StringUtil::split(ls.structname.fullname, '.');
        for (size_t i = 0; i < namespaces.size() - 1; ++i)
            emit(0, "Module %s", namespaces[i].c_str());
    }

    void emitPackageNamespaceClose()
    {
        auto namespaces = StringUtil::split(ls.structname.fullname, '.');
        for (size_t i = 0; i < namespaces.size() - 1; ++i)
            emit(0, "end\n");
    }

    void emitDependencies()
    {
        unordered_set<string> dependencies;
        for (auto& lm : ls.members) {
            auto& tn = lm.type.fullname;
            if (!ZCMGen::isPrimitiveType(tn) &&
                dependencies.find(tn) == dependencies.end() &&
                tn != ls.structname.fullname) {
                dependencies.insert(tn);
            }
        }

        for (auto& tn : dependencies) {
            emit(0, "include(\"%s.jl\")", dotsToSlashes(tn).c_str());
        }

        if (dependencies.size() > 0) emit(0, "");
    }

    void emitHeaderStart()
    {
        const char *sn = ls.structname.shortname.c_str();

        emitAutoGeneratedWarning();

        emitDependencies();

        emit(0, "");
        emitPackageNamespaceStart();

        // define the class
        emitComment(0, ls.comment);
        emit(0, "type %s", sn);
        emit(0, "");

        // data members
        if (ls.members.size() > 0) {
            emit(1, "# **********************");
            emit(1, "# Members");
            emit(1, "# **********************");
            for (auto& lm : ls.members) {
                auto& mtn = lm.type.fullname;
                emitComment(2, lm.comment);
                string mappedTypename = mapTypeName(mtn);
                int ndim = (int)lm.dimensions.size();
                if (ndim == 0) {
                    emit(1, "%-30s::%s", lm.membername.c_str(), mappedTypename.c_str());
                } else {
                    emit(1, "%-30s::Array{%s,%u}", lm.membername.c_str(),
                                                   mappedTypename.c_str(), ndim);
                }
            }
            emit(0, "");
        }

        // constants
        if (ls.constants.size() > 0) {
            emit(0, "");
            emit(1, "# **********************");
            emit(1, "# Constants");
            emit(1, "# **********************");
            for (auto& lc : ls.constants) {
                assert(ZCMGen::isLegalConstType(lc.type));
                string mt = mapTypeName(lc.type);
                emit(1, "%-30s::%s", lc.membername.c_str(), mt.c_str(), lc.valstr.c_str());
            }
            emit(0, "");
        }

        emit(1, "# **********************");
        emit(1, "# Functions");
        emit(1, "# **********************");

        emit(1, "%-30s::Function", "_get_hash_recursive");
        emit(1, "%-30s::Function", "getHash");
        emit(1, "%-30s::Function", "_encode_one");
        emit(1, "%-30s::Function", "encode");
        emit(1, "%-30s::Function", "__ntoh");
        emit(1, "%-30s::Function", "_decode_one");
        emit(1, "%-30s::Function", "decode");
        emit(0, "");
        emit(1, "function %s()", sn);
        emit(0, "");
        emit(2, "self = new();");
        emit(0, "");

        // data members
        if (ls.members.size() > 0) {
            emit(2, "# **********************");
            emit(2, "# Members");
            emit(2, "# **********************");
            for (size_t i = 0; i < ls.members.size(); ++i) {
                auto& lm = ls.members[i];
                emitStart(2, "self.%s = ", lm.membername.c_str());
                emitMemberInitializer(lm, 0);
                emitEnd("");
            }
            emit(0, "");
        }

        // constants
        if (ls.constants.size() > 0) {
            emit(2, "# **********************");
            emit(2, "# Constants");
            emit(2, "# **********************");

            for (auto& lc : ls.constants) {
                assert(ZCMGen::isLegalConstType(lc.type));
                string mt = mapTypeName(lc.type);
                emitStart(2, "self.%s::%s = ", lc.membername.c_str(), mt.c_str());
                if (lc.isFixedPoint())
                    emitEnd("reinterpret(%s,%s)", mt.c_str(), lc.valstr.c_str());
                else
                    emitEnd("%s", lc.valstr.c_str());
            }
            emit(0, "");
        }
    }

    void emitHeaderEnd()
    {
        emit(2, "return self");
        emit(1, "end");
        emit(0, "");
        emit(0, "end");
        emit(0, "");
        emitPackageNamespaceClose();
    }

    void emitMemberInitializer(ZCMMember& lm, int dimNum)
    {
        auto& mtn = lm.type.fullname;
        string mappedTypename = mapTypeName(mtn);

        if ((size_t)dimNum == lm.dimensions.size()) {
            auto& tn = lm.type.fullname;
            const char* initializer = nullptr;
            if (tn == "byte") initializer = "0";
            else if (tn == "boolean") initializer = "false";
            else if (tn == "int8_t")  initializer = "0";
            else if (tn == "int16_t") initializer = "0";
            else if (tn == "int32_t") initializer = "0";
            else if (tn == "int64_t") initializer = "0";
            else if (tn == "float")   initializer = "0.0";
            else if (tn == "double")  initializer = "0.0";
            else if (tn == "string")  initializer = "\"\"";

            if (initializer) {
                fprintfPass("%s", initializer);
            } else {
                fprintfPass("%s()", tn.c_str());
            }
            return;
        }
        auto& dim = lm.dimensions[dimNum];
        if (dim.mode == ZCM_VAR) {
            size_t dimLeft = lm.dimensions.size() - dimNum;
            fprintfPass("Array{%s,%lu}(", mappedTypename.c_str(), dimLeft);
            for (size_t i = 0; i < dimLeft - 1; ++i)
                fprintfPass("0,");
            fprintfPass("0)");
        } else {
            fprintfPass("[ ");
            emitMemberInitializer(lm, dimNum+1);
            fprintfPass(" for dim%d in range(1,%s) ]", dimNum, dim.size.c_str());
        }
    }

    void emitGetHash()
    {
        auto& sn_ = ls.structname.shortname;
        auto *sn = sn_.c_str();

        emit(2, "_hash::Int64 = 0");

        emit(2, "self._get_hash_recursive = function(parents::Array{String})");
        emit(3,     "if _hash != 0; return _hash; end");
        emit(3,     "if \"%s\"::String in parents; return 0; end", sn);
        for (auto& lm : ls.members) {
            if (!ZCMGen::isPrimitiveType(lm.type.fullname)) {
                emit(3, "newparents::Array{String} = [parents[:]; \"%s\"::String];", sn);
                break;
            }
        }
        emitStart(3, "hash::UInt64 = 0x%" PRIx64, ls.hash);
        for (auto &lm : ls.members) {
            auto& mn = lm.membername;
            if (!ZCMGen::isPrimitiveType(lm.type.fullname)) {
                const char *ghr = "_get_hash_recursive(newparents)";
                if (lm.type.fullname == ls.structname.fullname) {
                    emitContinue("+ reinterpret(UInt64, self.%s.%s)", mn.c_str(), ghr);
                } else {
                    if (lm.type.package != "")
                        emitContinue("+ reinterpret(UInt64, self.%s.%s)", mn.c_str(), ghr);
                    else
                        emitContinue("+ reinterpret(UInt64, self.%s.%s)", mn.c_str(), ghr);
                }
            }
        }
        emitEnd("");

        emit(3,     "hash = (hash << 1) + ((hash >>> 63) & 0x01)");
        emit(3,     "_hash = reinterpret(Int64, hash)");
        emit(3,     "return _hash");
        emit(2, "end");
        emit(0, "");
        emit(2, "self.getHash = function()", sn);
        emit(3,     "return self._get_hash_recursive(Array{String,1}([]))");
        emit(2, "end");
        emit(0, "");
    }

    void emitEncodeSingleMember(ZCMMember& lm, const string& accessor_, int indent)
    {
        const string& tn = lm.type.fullname;
        auto *accessor = accessor_.c_str();

        if (tn == "string") {
            emit(indent, "write(buf, hton(UInt32(length(%s) + 1)))", accessor);
            emit(indent, "write(buf, %s)", accessor);
            emit(indent, "write(buf, 0)");
        } else if (tn == "boolean") {
            emit(indent, "write(buf, %s)", accessor);
        } else if (tn == "byte"    || tn == "int8_t"  ||
                   tn == "int16_t" || tn == "int32_t" || tn == "int64_t" ||
                   tn == "float"   || tn == "double") {
            emit(indent, "write(buf, hton(%s))", accessor);
        } else {
            emit(indent, "%s._encode_one(buf)", accessor);
        }
    }

    void emitEncodeListMember(ZCMMember& lm, const string& accessor_, int indent,
                              const string& len_, int fixedLen)
    {
        auto& tn = lm.type.fullname;
        auto *accessor = accessor_.c_str();
        auto *len = len_.c_str();

        if (tn == "byte" || tn == "boolean" || tn == "int8_t" ||
            tn == "int16_t" || tn == "int32_t" || tn == "int64_t" ||
            tn == "float"  || tn == "double") {
            if (tn != "boolean")
                emit(indent, "for i in range(1,%s%s) %s[i] = hton(%s[i]) end",
                             (fixedLen ? "" : "self."), len, accessor, accessor);
            emit(indent, "write(buf, %s[1:%s%s])",
                 accessor, (fixedLen ? "" : "self."), len);
            return;
        } else {
            assert(0);
        }
    }

    void emitEncodeOne()
    {
        emit(2, "self._encode_one = function(buf)");
        if (ls.members.size() == 0) {
            emit(3, "return nothing");
            emit(2, "end");
            return;
        }

        for (auto& lm : ls.members) {
            if (lm.dimensions.size() == 0) {
                emitEncodeSingleMember(lm, "self." + lm.membername, 3);
            } else {
                string accessor = "self." + lm.membername;
                unsigned int n;
                for (n = 0; n < lm.dimensions.size() - 1; ++n) {
                    auto& dim = lm.dimensions[n];
                    accessor += "[i" + to_string(n) + "]";
                    if (dim.mode == ZCM_CONST) {
                        emit(n + 3, "for i%d in range(1,%s)", n, dim.size.c_str());
                    } else {
                        emit(n + 3, "for i%d in range(1,self.%s)", n, dim.size.c_str());
                    }
                }

                // last dimension.
                auto& lastDim = lm.dimensions[lm.dimensions.size() - 1];
                bool lastDimFixedLen = (lastDim.mode == ZCM_CONST);

                if (ZCMGen::isPrimitiveType(lm.type.fullname) &&
                    lm.type.fullname != "string") {
                    emitEncodeListMember(lm, accessor, n + 3, lastDim.size, lastDimFixedLen);
                } else {
                    if (lastDimFixedLen) {
                        emit(n + 3, "for i%d in range(1,%s)", n, lastDim.size.c_str());
                    } else {
                        emit(n + 3, "for i%d in range(1,self.%s)", n, lastDim.size.c_str());
                    }
                    accessor += "[i" + to_string(n) + "]";
                    emitEncodeSingleMember(lm, accessor, n + 5);
                    for (n = 0; n < lm.dimensions.size(); ++n)
                        emit(n + 3, "end");
                }
                for (n = 0; n < lm.dimensions.size() - 1; ++n)
                    emit(n + 3, "end");
            }
        }

        emit(2, "end");
    }

    void emitEncode()
    {
        emit(2, "self.encode = function()");
        emit(2, "    buf = IOBuffer()");
        emit(2, "    write(buf, hton(self.getHash()))");
        emit(2, "    self._encode_one(buf)");
        emit(2, "    return takebuf_array(buf);");
        emit(2, "end");
        emit(0, "");
    }

    void emitDecodeSingleMember(ZCMMember& lm, const string& accessor_,
                                int indent, const string& sfx_)
    {
        auto& tn = lm.type.fullname;
        string mappedTypename = mapTypeName(tn);
        auto& mn = lm.membername;

        auto *accessor = accessor_.c_str();
        auto *sfx = sfx_.c_str();

        if (tn == "string") {
            emit(indent, "__%s_len = ntoh(reinterpret(UInt32, read(buf, 4))[1])", mn.c_str());
            emit(indent, "%sString(read(buf, __%s_len))%s", accessor, mn.c_str(), sfx);
        } else if (tn == "byte"    || tn == "boolean" || tn == "int8_t") {
            auto typeSize = ZCMGen::getPrimitiveTypeSize(tn);
            emit(indent, "%sreinterpret(%s, read(buf, %u))[1]%s",
                         accessor, mappedTypename.c_str(), typeSize, sfx);
        } else if (tn == "int16_t" || tn == "int32_t" || tn == "int64_t" ||
                   tn == "float"   || tn == "double") {
            auto typeSize = ZCMGen::getPrimitiveTypeSize(tn);
            emit(indent, "%sntoh(reinterpret(%s, read(buf, %u))[1])%s",
                         accessor, mappedTypename.c_str(), typeSize, sfx);
        } else {
            emit(indent, "%sself.%s._decode_one(buf)%s", accessor, mn.c_str(), sfx);
        }
    }

    void emitDecodeListMember(ZCMMember& lm, const string& accessor_, int indent,
                              bool isFirst, const string& len_, bool fixedLen)
    {
        auto& tn = lm.type.fullname;
        string mappedTypename = mapTypeName(tn);
        const char *suffix = isFirst ? "" : ")";
        auto *accessor = accessor_.c_str();
        auto *len = len_.c_str();

        if (tn == "byte" || tn == "boolean" || tn == "int8_t" ) {
            if (fixedLen) {
                emit(indent, "%sreinterpret(%s, read(buf, %d))%s",
                     accessor, mappedTypename.c_str(),
                     atoi(len) * ZCMGen::getPrimitiveTypeSize(tn),
                     suffix);
            } else {
                emit(indent, "%sreinterpret(%s, read(buf, (self.%s) * %lu))%s",
                     accessor, mappedTypename.c_str(),
                     len, ZCMGen::getPrimitiveTypeSize(tn),
                     suffix);
            }
        } else if (tn == "int16_t" || tn == "int32_t" || tn == "int64_t" ||
                   tn == "float"   || tn == "double") {
            if (fixedLen) {
                emit(indent, "%sself.__ntoh(reinterpret(%s, read(buf, %d)))%s",
                     accessor, mappedTypename.c_str(),
                     atoi(len) * ZCMGen::getPrimitiveTypeSize(tn),
                     suffix);
            } else {
                emit(indent, "%sself.__ntoh(reinterpret(%s, read(buf, (self.%s) * %lu)))%s",
                     accessor, mappedTypename.c_str(),
                     len, ZCMGen::getPrimitiveTypeSize(tn),
                     suffix);
            }
        } else {
            assert(0);
        }
    }

    void emitDecodeOne()
    {
        emit(2, "self.__ntoh = function __ntoh{T}(arr::Array{T})");
        emit(2, "    for i in range(1,length(arr)) arr[i] = ntoh(arr[i]) end");
        emit(2, "    return arr");
        emit(2, "end");
        emit(2, "self._decode_one = function(buf)");

        for (auto& lm : ls.members) {
            if (lm.dimensions.size() == 0) {
                string accessor = "self." + lm.membername + " = ";
                emitDecodeSingleMember(lm, accessor.c_str(), 3, "");
            } else {
                string accessor = "self." + lm.membername;

                // iterate through the dimensions of the member, building up
                // an accessor string, and emitting for loops
                uint n = 0;
                for (n = 0; n < lm.dimensions.size()-1; n++) {
                    auto& dim = lm.dimensions[n];

                    if(n == 0) {
                        emit(3, "%s = []", accessor.c_str());
                    } else {
                        emit(n + 3, "%s.append([])", accessor.c_str());
                    }

                    if (dim.mode == ZCM_CONST) {
                        emit(n + 3, "for i%d in range(%s):", n, dim.size.c_str());
                    } else {
                        emit(n + 3, "for i%d in range(self.%s):", n, dim.size.c_str());
                    }

                    if(n > 0 && n < lm.dimensions.size()-1) {
                        accessor += "[i" + to_string(n - 1) + "]";
                    }
                }

                // last dimension.
                auto& lastDim = lm.dimensions[lm.dimensions.size()-1];
                bool lastDimFixedLen = (lastDim.mode == ZCM_CONST);

                if (ZCMGen::isPrimitiveType(lm.type.fullname) &&
                    lm.type.fullname != "string") {
                    // member is a primitive non-string type.  Emit code to
                    // decode a full array in one call to struct.unpack
                    if(n == 0) {
                        accessor += " = ";
                    } else {
                        accessor += ".append(";
                    }

                    emitDecodeListMember(lm, accessor, n + 3, n==0,
                                         lastDim.size, lastDimFixedLen);
                } else {
                    // member is either a string type or an inner ZCM type.  Each
                    // array element must be decoded individually
                    if(n == 0) {
                        emit(3, "%s = []", accessor.c_str());
                    } else {
                        emit(n + 3, "%s.append ([])", accessor.c_str());
                        accessor += "[i" + to_string(n-1) + "]";
                    }
                    if (lastDimFixedLen) {
                        emit(n + 3, "for i%d in range(%s):", n, lastDim.size.c_str());
                    } else {
                        emit(n + 3, "for i%d in range(self.%s):", n, lastDim.size.c_str());
                    }
                    accessor += ".append(";
                    emitDecodeSingleMember(lm, accessor, n+3, ")");
                }
            }
        }
        emit(3, "return self");
        emit(2, "end");
        emit(0, "");
    }

    void emitDecode()
    {
        emit(2, "self.decode = function(data::Array{UInt8,1})");
        emit(2, "    buf = IOBuffer(data)");
        emit(2, "    if ntoh(reinterpret(Int64, read(buf, 8))[1]) != self.getHash()");
        emit(2, "        throw(\"Decode error\")");
        emit(2, "    end");
        emit(2, "    return self._decode_one(buf)");
        emit(2, "end");
        emit(0, "");
    }

    void emitHeader()
    {
        emitHeaderStart();
        emitGetHash();
        emitEncodeOne();
        emitEncode();
        emitDecodeOne();
        emitDecode();
        emitHeaderEnd();
    }
};

int emitJulia(ZCMGen& zcm)
{
    if (zcm.gopt->getBool("little-endian-encoding")) {
        printf("Julia does not currently support little endian encoding\n");
        return -1;
    }

    // iterate through all defined message types
    for (auto& ls : zcm.structs) {
        string tn = dotsToSlashes(ls.structname.fullname);

        // compute the target filename
        string hpath = zcm.gopt->getString("julia-path");
        string headerName = hpath + (hpath.size() > 0 ? "/" : ":") + tn +".jl";

        // generate code if needed
        if (zcm.needsGeneration(ls.zcmfile, headerName)) {
            FileUtil::makeDirsForFile(headerName);
            EmitJulia E{zcm, ls, headerName};
            if (!E.good()) return -1;
            E.emitHeader();

        }
    }

    return 0;
}
