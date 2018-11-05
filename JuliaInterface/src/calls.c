#include "calls.h"
#include "convert.h"
#include "JuliaInterface.h"

#include <src/compiled.h>    // GAP headers

static Obj DoCallJuliaFunc0Arg(Obj func);
static Obj DoCallJuliaFunc0ArgConv(Obj func);


// Helper used to call GAP functions from Julia.
//
// This function is used by LibGAP.jl
jl_value_t * call_gap_func(Obj func, jl_value_t * args)
{
    if (!jl_is_tuple(args))
        jl_error("<args> must be a tuple");

    size_t len = jl_nfields(args);
    Obj    return_value = NULL;
    if (IS_FUNC(func) && len <= 6) {
        switch (len) {
        case 0:
            return_value = CALL_0ARGS(func);
            break;
        case 1:
            return_value = CALL_1ARGS(func, gap_julia(jl_fieldref(args, 0)));
            break;
        case 2:
            return_value = CALL_2ARGS(func, gap_julia(jl_fieldref(args, 0)),
                                      gap_julia(jl_fieldref(args, 1)));
            break;
        case 3:
            return_value = CALL_3ARGS(func, gap_julia(jl_fieldref(args, 0)),
                                      gap_julia(jl_fieldref(args, 1)),
                                      gap_julia(jl_fieldref(args, 2)));
            break;
        case 4:
            return_value = CALL_4ARGS(func, gap_julia(jl_fieldref(args, 0)),
                                      gap_julia(jl_fieldref(args, 1)),
                                      gap_julia(jl_fieldref(args, 2)),
                                      gap_julia(jl_fieldref(args, 3)));
            break;
        case 5:
            return_value = CALL_5ARGS(func, gap_julia(jl_fieldref(args, 0)),
                                      gap_julia(jl_fieldref(args, 1)),
                                      gap_julia(jl_fieldref(args, 2)),
                                      gap_julia(jl_fieldref(args, 3)),
                                      gap_julia(jl_fieldref(args, 4)));
            break;
        case 6:
            return_value = CALL_6ARGS(func, gap_julia(jl_fieldref(args, 0)),
                                      gap_julia(jl_fieldref(args, 1)),
                                      gap_julia(jl_fieldref(args, 2)),
                                      gap_julia(jl_fieldref(args, 3)),
                                      gap_julia(jl_fieldref(args, 4)),
                                      gap_julia(jl_fieldref(args, 5)));
            break;
        }
    }
    else {
        Obj arg_list = NEW_PLIST(T_PLIST, len);
        SET_LEN_PLIST(arg_list, len);
        for (size_t i = 0; i < len; i++) {
            SET_ELM_PLIST(arg_list, i + 1, gap_julia(jl_fieldref(args, i)));
            CHANGED_BAG(arg_list);
        }
        return_value = CallFuncList(func, arg_list);
    }
    if (return_value == NULL) {
        return jl_nothing;
    }
    return julia_gap(return_value);
}


inline Int IS_JULIA_FUNC(Obj obj)
{
    return IS_FUNC(obj) && (HDLR_FUNC(obj, 0) == DoCallJuliaFunc0Arg ||
                            HDLR_FUNC(obj, 0) == DoCallJuliaFunc0ArgConv);
}

inline jl_function_t * GET_JULIA_FUNC(Obj obj)
{
    GAP_ASSERT(IS_JULIA_FUNC(obj));
    // TODO
    return (jl_function_t *)FEXS_FUNC(obj);
}

static ALWAYS_INLINE Obj DoCallJuliaFunc(Obj       func,
                                         const int narg,
                                         Obj *     a,
                                         const int autoConvert)
{
    jl_value_t * result;

    if (autoConvert) {
        for (int i = 0; i < narg; i++) {
            a[i] = (Obj)_ConvertedToJulia_internal(a[i]);
        }
    }
    else {
        for (int i = 0; i < narg; i++) {
            if (IS_INTOBJ(a[i]))
                a[i] = (Obj)jl_box_int64(INT_INTOBJ(a[i]));
            else if (IS_FFE(a[i]))
                ErrorQuit("TODO: implement conversion for T_FFE", 0, 0);
        }
    }
    jl_function_t * f = GET_JULIA_FUNC(func);
    switch (narg) {
    case 0:
        result = jl_call0(f);
        break;
    case 1:
        result = jl_call1(f, (jl_value_t *)a[0]);
        break;
    case 2:
        result = jl_call2(f, (jl_value_t *)a[0], (jl_value_t *)a[1]);
        break;
    case 3:
        result = jl_call3(f, (jl_value_t *)a[0], (jl_value_t *)a[1],
                          (jl_value_t *)a[2]);
        break;
    default:
        result = jl_call(f, (jl_value_t **)a, narg);
    }
    // It suffices to use JULIAINTERFACE_EXCEPTION_HANDLER here, as jl_call
    // and its variants are part of the jlapi, so don't have to be wrapped in
    // JL_TRY/JL_CATCH.
    JULIAINTERFACE_EXCEPTION_HANDLER
    if (IsGapObj(result))
        return (Obj)result;
    return NewJuliaObj(result);
}

static Obj DoCallJuliaFunc0ArgConv(Obj func)
{
    return DoCallJuliaFunc(func, 0, 0, 1);
}

static Obj DoCallJuliaFunc1ArgConv(Obj func, Obj arg1)
{
    Obj a[] = { arg1 };
    return DoCallJuliaFunc(func, 1, a, 1);
}

static Obj DoCallJuliaFunc2ArgConv(Obj func, Obj arg1, Obj arg2)
{
    Obj a[] = { arg1, arg2 };
    return DoCallJuliaFunc(func, 2, a, 1);
}

static Obj DoCallJuliaFunc3ArgConv(Obj func, Obj arg1, Obj arg2, Obj arg3)
{
    Obj a[] = { arg1, arg2, arg3 };
    return DoCallJuliaFunc(func, 3, a, 1);
}

static Obj
DoCallJuliaFunc4ArgConv(Obj func, Obj arg1, Obj arg2, Obj arg3, Obj arg4)
{
    Obj a[] = { arg1, arg2, arg3, arg4 };
    return DoCallJuliaFunc(func, 4, a, 1);
}

static Obj DoCallJuliaFunc5ArgConv(
    Obj func, Obj arg1, Obj arg2, Obj arg3, Obj arg4, Obj arg5)
{
    Obj a[] = { arg1, arg2, arg3, arg4, arg5 };
    return DoCallJuliaFunc(func, 5, a, 1);
}

static Obj DoCallJuliaFunc6ArgConv(
    Obj func, Obj arg1, Obj arg2, Obj arg3, Obj arg4, Obj arg5, Obj arg6)
{
    Obj a[] = { arg1, arg2, arg3, arg4, arg5, arg6 };
    return DoCallJuliaFunc(func, 6, a, 1);
}

static Obj DoCallJuliaFuncXArgConv(Obj func, Obj args)
{
    const int len = LEN_PLIST(args);
    Obj       a[len];
    for (int i = 0; i < len; i++) {
        a[i] = ELM_PLIST(args, i + 1);
    }
    return DoCallJuliaFunc(func, len, a, 1);
}

//
//
//


static Obj DoCallJuliaFunc0Arg(Obj func)
{
    return DoCallJuliaFunc(func, 0, 0, 0);
}

static Obj DoCallJuliaFunc1Arg(Obj func, Obj arg1)
{
    Obj a[] = { arg1 };
    return DoCallJuliaFunc(func, 1, a, 0);
}

static Obj DoCallJuliaFunc2Arg(Obj func, Obj arg1, Obj arg2)
{
    Obj a[] = { arg1, arg2 };
    return DoCallJuliaFunc(func, 2, a, 0);
}

static Obj DoCallJuliaFunc3Arg(Obj func, Obj arg1, Obj arg2, Obj arg3)
{
    Obj a[] = { arg1, arg2, arg3 };
    return DoCallJuliaFunc(func, 3, a, 0);
}

static Obj
DoCallJuliaFunc4Arg(Obj func, Obj arg1, Obj arg2, Obj arg3, Obj arg4)
{
    Obj a[] = { arg1, arg2, arg3, arg4 };
    return DoCallJuliaFunc(func, 4, a, 0);
}

static Obj DoCallJuliaFunc5Arg(
    Obj func, Obj arg1, Obj arg2, Obj arg3, Obj arg4, Obj arg5)
{
    Obj a[] = { arg1, arg2, arg3, arg4, arg5 };
    return DoCallJuliaFunc(func, 5, a, 0);
}

static Obj DoCallJuliaFunc6Arg(
    Obj func, Obj arg1, Obj arg2, Obj arg3, Obj arg4, Obj arg5, Obj arg6)
{
    Obj a[] = { arg1, arg2, arg3, arg4, arg5, arg6 };
    return DoCallJuliaFunc(func, 6, a, 0);
}

static Obj DoCallJuliaFuncXArg(Obj func, Obj args)
{
    const int len = LEN_PLIST(args);
    Obj       a[len];
    for (int i = 0; i < len; i++) {
        a[i] = ELM_PLIST(args, i + 1);
    }
    return DoCallJuliaFunc(func, len, a, 0);
}


//
//
//
Obj NewJuliaFunc(jl_function_t * function, int autoConvert)
{
    // TODO: set a sensible name?
    //     jl_datatype_t * dt = ...
    //     jl_typename_t * tname = dt->name;
    //     //    struct _jl_methtable_t *mt;
    //     jl_sym_t *name = tname->mt->name;

    Obj func = NewFunctionC("", -1, "arg", 0);

    SET_HDLR_FUNC(
        func, 0, autoConvert ? DoCallJuliaFunc0ArgConv : DoCallJuliaFunc0Arg);
    SET_HDLR_FUNC(
        func, 1, autoConvert ? DoCallJuliaFunc1ArgConv : DoCallJuliaFunc1Arg);
    SET_HDLR_FUNC(
        func, 2, autoConvert ? DoCallJuliaFunc2ArgConv : DoCallJuliaFunc2Arg);
    SET_HDLR_FUNC(
        func, 3, autoConvert ? DoCallJuliaFunc3ArgConv : DoCallJuliaFunc3Arg);
    SET_HDLR_FUNC(
        func, 4, autoConvert ? DoCallJuliaFunc4ArgConv : DoCallJuliaFunc4Arg);
    SET_HDLR_FUNC(
        func, 5, autoConvert ? DoCallJuliaFunc5ArgConv : DoCallJuliaFunc5Arg);
    SET_HDLR_FUNC(
        func, 6, autoConvert ? DoCallJuliaFunc6ArgConv : DoCallJuliaFunc6Arg);
    SET_HDLR_FUNC(
        func, 7, autoConvert ? DoCallJuliaFuncXArgConv : DoCallJuliaFuncXArg);

    // trick: fexs is unused for kernel functions, so we can store
    // the Julia function point in here
    SET_FEXS_FUNC(func, (Obj)function);

    return func;
}

/*
 * C function pointer calls
 */

static inline ObjFunc get_c_function_pointer(Obj func)
{
    return jl_unbox_voidpointer((jl_value_t *)FEXS_FUNC(func));
}

static Obj DoCallJuliaCFunc0Arg(Obj func)
{
    ObjFunc function = get_c_function_pointer(func);
    Obj     result;
    BEGIN_JULIA
        result = function();
    END_JULIA
    return result;
}

static Obj DoCallJuliaCFunc1Arg(Obj func, Obj arg1)
{
    ObjFunc function = get_c_function_pointer(func);
    Obj     result;
    BEGIN_JULIA
        result = function(arg1);
    END_JULIA
    return result;
}

static Obj DoCallJuliaCFunc2Arg(Obj func, Obj arg1, Obj arg2)
{
    ObjFunc function = get_c_function_pointer(func);
    Obj     result;
    BEGIN_JULIA
        result = function(arg1, arg2);
    END_JULIA
    return result;
}

static Obj DoCallJuliaCFunc3Arg(Obj func, Obj arg1, Obj arg2, Obj arg3)
{
    ObjFunc function = get_c_function_pointer(func);
    Obj     result;
    BEGIN_JULIA
        result = function(arg1, arg2, arg3);
    END_JULIA
    return result;
}

static Obj
DoCallJuliaCFunc4Arg(Obj func, Obj arg1, Obj arg2, Obj arg3, Obj arg4)
{
    ObjFunc function = get_c_function_pointer(func);
    Obj     result;
    BEGIN_JULIA
        result = function(arg1, arg2, arg3, arg4);
    END_JULIA
    return result;
}

static Obj DoCallJuliaCFunc5Arg(
    Obj func, Obj arg1, Obj arg2, Obj arg3, Obj arg4, Obj arg5)
{
    ObjFunc function = get_c_function_pointer(func);
    Obj     result;
    BEGIN_JULIA
        result = function(arg1, arg2, arg3, arg4, arg5);
    END_JULIA
    return result;
}

static Obj DoCallJuliaCFunc6Arg(
    Obj func, Obj arg1, Obj arg2, Obj arg3, Obj arg4, Obj arg5, Obj arg6)
{
    ObjFunc function = get_c_function_pointer(func);
    Obj     result;
    BEGIN_JULIA
        result = function(arg1, arg2, arg3, arg4, arg5, arg6);
    END_JULIA
    return result;
}


Obj NewJuliaCFunc(void * function, Obj arg_names)
{
    ObjFunc handler;

    switch (LEN_PLIST(arg_names)) {
    case 0:
        handler = DoCallJuliaCFunc0Arg;
        break;
    case 1:
        handler = DoCallJuliaCFunc1Arg;
        break;
    case 2:
        handler = DoCallJuliaCFunc2Arg;
        break;
    case 3:
        handler = DoCallJuliaCFunc3Arg;
        break;
    case 4:
        handler = DoCallJuliaCFunc4Arg;
        break;
    case 5:
        handler = DoCallJuliaCFunc5Arg;
        break;
    case 6:
        handler = DoCallJuliaCFunc6Arg;
        break;
    default:
        ErrorQuit("Only 0-6 arguments are supported", 0, 0);
        break;
    }

    Obj func = NewFunction(0, LEN_PLIST(arg_names), arg_names, handler);

    // trick: fexs is unused for kernel functions, so we can store
    // the function pointer here. Since fexs gets marked by the GC, we
    // store it as a valid julia obj (i.e., void ptr).
    SET_FEXS_FUNC(func, (Obj)jl_box_voidpointer(function));

    return func;
}