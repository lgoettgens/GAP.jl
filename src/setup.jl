module Setup

using Pkg: GitTools
using Artifacts
using GAP_jll
using GAP_lib_jll
using GAP_pkg_juliainterface_jll
using Scratch
import Pidfile

# to separate the scratchspaces of different GAP.jl copies and Julia versions
# put the Julia version and the hash of the path to this file into the key
const scratch_key = "gap_$(string(hash(@__FILE__)))_$(VERSION.major).$(VERSION.minor)"

gaproot() = @get_scratch!(scratch_key)

#############################################################################
#
# Set up the primary, mutable GAP root
#
# For this we read the sysinfo.gap bundled with GAP_jll and then modify it
# to be usable on this computer
#
#############################################################################

# ensure `source` is a symlink pointing to `target` in a way that is hopefully
# safe against races with other Julia processes doing the exact same thing
function force_symlink(source::AbstractString, target::AbstractString)
    # We previously used `rm` followed by `symlink`, but this can cause a
    # race if multiple processes invoke `rm` concurrently (which works if
    # one uses `force=true`), and then try to invoke `symlink`
    # concurrently (which then fails in all but one process).
    #
    # So instead we create the symlink with a temporary name, and then use
    # an atomic `rename` to rename it to the `target` name. The latter
    # unfortunately requires invoking an undocumented function.
    tmpfile = tempname(dirname(abspath(target)); cleanup=false)
    symlink(source, tmpfile)
    Base.Filesystem.rename(tmpfile, target)
    return nothing
end

function read_sysinfo_gap(dir::String)
    d = Dict{String,String}()
    open(joinpath(dir, "sysinfo.gap")) do file
        for ln in eachline(file)
            if length(ln) == 0 || ln[1] == '#'
                continue
            end
            s = split(ln, "=")
            if length(s) != 2
                continue
            end
            d[s[1]] = strip(s[2], ['"'])
        end
    end
    return d
end

function select_compiler(lang, candidates, extension)
    tmpfilename = tempname()
    open(tmpfilename * extension, "w") do file
        write(file, """
        #include <stdio.h>
        int main(int argc, char **argv) {
          return 0;
        }
        """)
    end
    for compiler in candidates
        try
            rm(tmpfilename; force = true)
            run(`$(compiler) -o $(tmpfilename) $(tmpfilename)$(extension)`)
            run(`$(tmpfilename)`)
            @debug "selected $(compiler) as $(lang) compiler"
            return compiler
        catch
            @debug "$(lang) compiler candidate '$(compiler)' not working"
        end
    end
    @debug "Could not locate a working $(lang) compiler"
    return first(candidates)
end

include("julia-config.jl")

function regenerate_gaproot()
    gaproot_mutable = gaproot()

    gaproot_gapjl = abspath(@__DIR__, "..")
    @debug "Set up gaproot at $(gaproot_mutable)"

    gap_prefix = GAP_jll.find_artifact_dir()

    # load the existing sysinfo.gap
    sysinfo = read_sysinfo_gap(joinpath(gap_prefix, "lib", "gap"))
    sysinfo_keys = collect(keys(sysinfo))

    #
    # now we modify sysinfo for our needs
    #

    # Locate C compiler (for use by GAP packages)
    cc_candidates = [ "cc", "gcc", "clang" ]
    haskey(ENV, "CC") && pushfirst!(cc_candidates, ENV["CC"])
    haskey(ENV, "GAP_CC") && pushfirst!(cc_candidates, ENV["GAP_CC"])
    CC = sysinfo["GAP_CC"] = select_compiler("C", cc_candidates, ".c")

    # Locate  C++ compiler (for use by GAP packages)
    cxx_candidates = [ "c++", "g++", "clang++" ]
    haskey(ENV, "CXX") && pushfirst!(cxx_candidates, ENV["CXX"])
    haskey(ENV, "GAP_CXX") && pushfirst!(GAP_CXX_candidates, ENV["GAP_CXX"])
    CXX = sysinfo["GAP_CXX"] = select_compiler("C++", cxx_candidates, ".cc")

    sysinfo["GAP_CFLAGS"] = " -g -O2"
    sysinfo["GAP_CXXFLAGS"] = " -g -O2"

    # set include flags
    gap_include = joinpath(gap_prefix, "include", "gap")
    gap_include2 = joinpath(gaproot_mutable) # for code doing `#include "src/compiled.h"`
    sysinfo["GAP_CPPFLAGS"] = "-I$(gap_include) -I$(gap_include2) -DUSE_JULIA_GC=1"

    # set linker flags; since these are meant for use for GAP packages, add the necessary
    # flags to link against libgap
    gap_lib = joinpath(gap_prefix, "lib")
    sysinfo["GAP_LDFLAGS"] = "-L$(gap_lib) -lgap"

    # adjust linker flags for GAP kernel extensions on macOS
    if Sys.isapple()
        sysinfo["GAC_LDFLAGS"] = "-bundle -L$(gap_lib) -lgap"
    end

    GAP_VERSION = VersionNumber(sysinfo["GAP_VERSION"])
    gaproot_packages = joinpath(Base.DEPOT_PATH[1], "gaproot", "v$(GAP_VERSION.major).$(GAP_VERSION.minor)")
    sysinfo["DEFAULT_PKGDIR"] = joinpath(gaproot_packages, "pkg")
    mkpath(sysinfo["DEFAULT_PKGDIR"])
    gap_lib_dir = abspath(GAP_lib_jll.find_artifact_dir(), "share", "gap")
    roots = [
            gaproot_gapjl,          # for JuliaInterface and JuliaExperimental
            gaproot_packages,       # default installation dir for PackageManager
            gaproot_mutable,
            gap_lib_dir, # the actual GAP library, from GAP_lib_jll
            ]
    sysinfo["GAPROOTS"] = join(roots, ";")

    # path to gap & gac (used by some package build systems)
    sysinfo["GAP"] = joinpath(gaproot_mutable, "bin", "gap.sh")
    sysinfo["GAC"] = joinpath(gaproot_mutable, "gac")

    # create the mutable gaproot
    mkpath(gaproot_mutable)
    Pidfile.mkpidlock("$gaproot_mutable.lock") do
        # create fake sysinfo.gap
        unquoted = Set(["GAParch", "GAP_ABI", "GAP_HPCGAP", "GAP_KERNEL_MAJOR_VERSION", "GAP_KERNEL_MINOR_VERSION", "GAP_OBJEXT"])
        open("$gaproot_mutable/sysinfo.gap", "w") do file
            write(file, """
            # This file has been generated by the GAP build system,
            # do not edit manually!
            """)
            for key in sort(collect(keys(sysinfo)))
                if key in unquoted
                    str = "$(key)=$(sysinfo[key])"
                else
                    str = "$(key)=\"$(sysinfo[key])\""
                end
                write(file, str, "\n")
            end
        end

        # patch gac to load correct sysinfo.gap
        gac = read(joinpath(gap_prefix, "bin", "gac"), String)
        gac = replace(gac, r"^\. \"[^\"]+\"$"m => ". \"$(gaproot_mutable)/sysinfo.gap\"")
        write("$gaproot_mutable/gac", gac)
        chmod("$gaproot_mutable/gac", 0o755)

        # 
        mkpath(joinpath(gaproot_mutable, "bin"))
        for d in (("include/gap", "src"), ("lib", "lib"), ("bin/gap", "gap"))
            force_symlink(joinpath(gap_prefix, d[1]), joinpath(gaproot_mutable, d[2]))
        end

        # emulate the "compat mode" of the GAP build system, to help certain
        # packages like Browse with an outdated build system
        mkpath(joinpath(gaproot_mutable, "bin", sysinfo["GAParch"]))
        force_symlink("../../gac",
                      joinpath(gaproot_mutable, "bin", sysinfo["GAParch"], "gac"))

        # for building GAP packages
        force_symlink(joinpath(gaproot_gapjl, "etc", "BuildPackages.sh"),
                      joinpath(gaproot_mutable, "bin", "BuildPackages.sh"))

        # create a `pkg` symlink to the GAP packages artifact
        force_symlink(artifact"gap_packages", "$gaproot_mutable/pkg")

    end # mkpidlock

    return sysinfo
end

function build_JuliaInterface(sysinfo::Dict{String, String})
    @info "Compiling JuliaInterface ..."

    # run code in julia-config.jl to determine compiler and linker flags for Julia;
    # remove apostrophes, they mess up quoting when used in shell code(although
    # they are fine inside of Makefiles); this could cause problems if any
    # paths involve spaces, but then we likely will haves problem in other
    # places; in any case, if anybody ever cares about this, we can work on
    # finding a better solution.
    JULIA_CPPFLAGS = filter(c -> c != '\'', cflags())
    JULIA_LDFLAGS = filter(c -> c != '\'', ldflags())
    JULIA_LIBS = filter(c -> c != '\'', ldlibs())

    jipath = joinpath(@__DIR__, "..", "pkg", "JuliaInterface")
    cd(jipath) do
        withenv("CFLAGS" => JULIA_CPPFLAGS,
                "LDFLAGS" => JULIA_LDFLAGS * " " * JULIA_LIBS) do
            run(pipeline(`./configure $(gaproot())`, stdout="build.log"))
            run(pipeline(`make V=1 -j$(Sys.CPU_THREADS)`, stdout="build.log", append=true))
        end
    end

    return normpath(joinpath(jipath, "bin", sysinfo["GAParch"]))
end

function locate_JuliaInterface_so(sysinfo::Dict{String, String})
    # compare the C sources used to build GAP_pkg_juliainterface_jll with bundled copies
    # by comparing tree hashes
    jll = GAP_pkg_juliainterface_jll.find_artifact_dir()
    jll_hash = GitTools.tree_hash(joinpath(jll, "src"))
    bundled = joinpath(@__DIR__, "..", "pkg", "JuliaInterface")
    bundled_hash = GitTools.tree_hash(joinpath(bundled, "src"))
    if jll_hash == bundled_hash
        # if the tree hashes match then we can use JuliaInterface.so from the JLL
        @debug "Use JuliaInterface.so from GAP_pkg_juliainterface_jll"
        path = joinpath(jll, "lib", "gap")
    else
        # tree hashes differ: we must compile the bundled sources
        path = build_JuliaInterface(sysinfo)
        @debug "Use JuliaInterface.so from $(path)"
    end
    return joinpath(path, "JuliaInterface.so")
end

end # module

"""
    create_gap_sh(dstdir::String)

Given a directory path, create three files in that directory:
- a shell script named `gap.sh` which acts like the `gap.sh` shipped with a
  regular GAP installation, but which behind the scenes launches GAP via Julia.
- two TOML files, `Manifest.toml` and `Project.toml`, which are required by
  `gap.sh` to function (they record the precise versions of GAP.jl and other
  Julia packages involved)
"""
function create_gap_sh(dstdir::String)

    mkpath(dstdir)

    gaproot_gapjl = abspath(@__DIR__, "..")

    ##
    ## Create Project.toml & Manifest.toml for use by gap.sh
    ##
    @info "Generating custom Julia project ..."
    run(pipeline(`$(Base.julia_cmd()) --startup-file=no --project=$(dstdir) -e "using Pkg; Pkg.develop(PackageSpec(path=\"$(gaproot_gapjl)\"))"`))

    ##
    ## Create custom gap.sh
    ##
    @info "Generating gap.sh ..."

    gap_sh_path = joinpath(dstdir, "gap.sh")
    write(gap_sh_path,
        """
        #!/bin/sh
        # This is a a Julia script which also is a valid bash script; if executed by
        # bash, it will execute itself by invoking `julia`. Of course this only works
        # right if `julia` exists in the PATH and is the "correct" julia executable.
        # But you can always instead load this file as if it was a .jl file via any
        # other Julia executable.
        #=
        exec $(joinpath(Sys.BINDIR, Base.julia_exename())) --startup-file=no --project=$(dstdir) -i -- "$(gap_sh_path)" "\$@"
        =#

        # pass command line arguments to GAP.jl via a small hack
        ENV["GAP_PRINT_BANNER"] = "true"
        __GAP_ARGS__ = ARGS
        using GAP
        exit(GAP.run_session())
        """,
        )
    chmod(gap_sh_path, 0o755)

end # function
