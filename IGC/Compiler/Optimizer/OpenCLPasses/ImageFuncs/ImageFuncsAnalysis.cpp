/*===================== begin_copyright_notice ==================================

Copyright (c) 2017 Intel Corporation

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


======================= end_copyright_notice ==================================*/

#include "Compiler/Optimizer/OpenCLPasses/ImageFuncs/ImageFuncsAnalysis.hpp"
#include "Compiler/Optimizer/OCLBIUtils.h"
#include "Compiler/IGCPassSupport.h"

#include <set>

using namespace llvm;
using namespace IGC;

// Register pass to igc-opt
#define PASS_FLAG "igc-image-func-analysis"
#define PASS_DESCRIPTION "Analyzes image height, width, depth functions"
#define PASS_CFG_ONLY false
#define PASS_ANALYSIS false
IGC_INITIALIZE_PASS_BEGIN(ImageFuncsAnalysis, PASS_FLAG, PASS_DESCRIPTION, PASS_CFG_ONLY, PASS_ANALYSIS)
IGC_INITIALIZE_PASS_DEPENDENCY(MetaDataUtilsWrapper)
IGC_INITIALIZE_PASS_END(ImageFuncsAnalysis, PASS_FLAG, PASS_DESCRIPTION, PASS_CFG_ONLY, PASS_ANALYSIS)

char ImageFuncsAnalysis::ID = 0;

ImageFuncsAnalysis::ImageFuncsAnalysis() : ModulePass(ID)
{
    initializeImageFuncsAnalysisPass(*PassRegistry::getPassRegistry());
}

// All image functions needed resolved by implicit arguments
const llvm::StringRef ImageFuncsAnalysis::GET_IMAGE_HEIGHT = "__builtin_IB_get_image_height";
const llvm::StringRef ImageFuncsAnalysis::GET_IMAGE_WIDTH = "__builtin_IB_get_image_width";
const llvm::StringRef ImageFuncsAnalysis::GET_IMAGE_DEPTH = "__builtin_IB_get_image_depth";
const llvm::StringRef ImageFuncsAnalysis::GET_IMAGE_NUM_MIP_LEVELS = "__builtin_IB_get_image_num_mip_levels";
const llvm::StringRef ImageFuncsAnalysis::GET_IMAGE_CHANNEL_DATA_TYPE = "__builtin_IB_get_image_channel_data_type";
const llvm::StringRef ImageFuncsAnalysis::GET_IMAGE_CHANNEL_ORDER = "__builtin_IB_get_image_channel_order";
const llvm::StringRef ImageFuncsAnalysis::GET_IMAGE_SRGB_CHANNEL_ORDER = "__builtin_IB_get_image_srgb_channel_order";
const llvm::StringRef ImageFuncsAnalysis::GET_IMAGE_ARRAY_SIZE = "__builtin_IB_get_image_array_size";
const llvm::StringRef ImageFuncsAnalysis::GET_IMAGE_NUM_SAMPLES = "__builtin_IB_get_image_num_samples";
const llvm::StringRef ImageFuncsAnalysis::GET_SAMPLER_ADDRESS_MODE = "__builtin_IB_get_address_mode";
const llvm::StringRef ImageFuncsAnalysis::GET_SAMPLER_NORMALIZED_COORDS = "__builtin_IB_is_normalized_coords";
const llvm::StringRef ImageFuncsAnalysis::GET_SAMPLER_SNAP_WA_REQUIRED = "__builtin_IB_get_snap_wa_reqd";

bool ImageFuncsAnalysis::runOnModule(Module &M) {
    bool changed = false;
    // Run on all functions defined in this module
    for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I) {
        Function* pFunc = &(*I);
        if (pFunc->isDeclaration()) continue;
        if (runOnFunction(*pFunc))
        {
            changed = true;
        }
    }

    return changed;
}

bool ImageFuncsAnalysis::runOnFunction(Function &F) {
    
    // Visit the function
    visit(F);

    ImplicitArgs::addImageArgs(F, m_argMap, getAnalysis<MetaDataUtilsWrapper>().getMetaDataUtils());

    m_argMap.clear();

    return true;
}

void ImageFuncsAnalysis::visitCallInst(CallInst &CI)
{
    if (!CI.getCalledFunction())
    {
        return;
    }

    StringRef funcName = CI.getCalledFunction()->getName();

    // Check for OpenCL image dimension function calls
    std::set<int>* imageFunc = nullptr;

    if(funcName == GET_IMAGE_HEIGHT) 
    {
        imageFunc = &m_argMap[ImplicitArg::IMAGE_HEIGHT];
    }
    else if(funcName == GET_IMAGE_WIDTH)
    {
        imageFunc = &m_argMap[ImplicitArg::IMAGE_WIDTH];
    }
    else if(funcName == GET_IMAGE_DEPTH)
    {
        imageFunc = &m_argMap[ImplicitArg::IMAGE_DEPTH];
    }
    else if(funcName == GET_IMAGE_NUM_MIP_LEVELS)
    {
        imageFunc = &m_argMap[ImplicitArg::IMAGE_NUM_MIP_LEVELS];
    }
    else if(funcName == GET_IMAGE_CHANNEL_DATA_TYPE)
    {
        imageFunc = &m_argMap[ImplicitArg::IMAGE_CHANNEL_DATA_TYPE];
    }
    else if(funcName == GET_IMAGE_CHANNEL_ORDER)
    {
        imageFunc = &m_argMap[ImplicitArg::IMAGE_CHANNEL_ORDER];
    }
    else if(funcName == GET_IMAGE_SRGB_CHANNEL_ORDER)
    {
        imageFunc = &m_argMap[ImplicitArg::IMAGE_SRGB_CHANNEL_ORDER];
    }
    else if(funcName == GET_IMAGE_ARRAY_SIZE)
    {
        imageFunc = &m_argMap[ImplicitArg::IMAGE_ARRAY_SIZE];
    }
    else if (funcName == GET_IMAGE_NUM_SAMPLES)
    {
        imageFunc = &m_argMap[ImplicitArg::IMAGE_NUM_SAMPLES];
    }
    else if(funcName == GET_SAMPLER_ADDRESS_MODE)
    {
        imageFunc = &m_argMap[ImplicitArg::SAMPLER_ADDRESS];
    }
    else if(funcName == GET_SAMPLER_NORMALIZED_COORDS)
    {
        imageFunc = &m_argMap[ImplicitArg::SAMPLER_NORMALIZED];
    }
    else if(funcName == GET_SAMPLER_SNAP_WA_REQUIRED)
    {
        imageFunc = &m_argMap[ImplicitArg::SAMPLER_SNAP_WA];
    }
    else 
    {
        // Non image function, do nothing
        return;
    }

    // Extract the arg num and add it to the appropriate data structure    
    assert(CI.getNumArgOperands() == 1 && "Supported image/sampler functions are expected\
                                           to have only one argument");
    
    // We only care about image and sampler arguments here, inline samplers
    // don't require extra kernel parameters.
    Value* callArg = CImagesBI::CImagesUtils::traceImageOrSamplerArgument(&CI, 0, getAnalysis<MetaDataUtilsWrapper>().getMetaDataUtils());

    // TODO: For now assume that we may not trace a sampler/texture for indirect access. 
    // In this case we provide no WA support for indirect case and all WAs will return 0.
    // These WAs need to be reworked to support indirect case in the future.
    if (callArg)
    {
        if (Argument* arg = dyn_cast<Argument>(callArg))
        {
            imageFunc->insert(arg->getArgNo());
        }
    }
    else
    {
        // Only these args should be hit by the indirect case
        assert(funcName == GET_SAMPLER_NORMALIZED_COORDS || funcName == GET_SAMPLER_SNAP_WA_REQUIRED);
    }
}