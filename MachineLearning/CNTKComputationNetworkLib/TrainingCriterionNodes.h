//
// <copyright file="TrainingCriterionNodes.h" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//
#pragma once

#include <map>
#include <string>
#include <vector>
#include <stdexcept>
#include <list>
#include <memory>
#include "ComputationNode.h"
#include "InputAndParamNodes.h"
#include "gammacalculation.h"

namespace Microsoft { namespace MSR { namespace CNTK {

    // -----------------------------------------------------------------------
    /// SquareErrorNode (left, right)
    // -----------------------------------------------------------------------

    //note: to save computation the gradient may be scaled by an constant. 

    template<class ElemType>
    class SquareErrorNode : public ComputationNodeNonLooping/*ComputationNode*/<ElemType>, public NumInputs<2>
    {
        typedef ComputationNodeNonLooping<ElemType> Base; UsingComputationNodeMembersBoilerplate;
        static const std::wstring TypeName() { return L"SquareError"; }
    public:
        SquareErrorNode(DEVICEID_TYPE deviceId, const wstring & name) :
            Base(deviceId, name),
            m_leftMinusRight(deviceId)
        { }

        virtual void ComputeInputPartial(const size_t inputIndex)
        {
            if (inputIndex > 1)
                InvalidArgument("SquareError criteria only takes two inputs.");

            if (inputIndex == 0)  //left derivative
                ComputeInputPartialLeft(Inputs(0)->GradientValues(), GradientValues(), m_leftMinusRight);
            else
                ComputeInputPartialRight(Inputs(1)->GradientValues(), GradientValues(), m_leftMinusRight);
        }

        /*TODO: merge with call site*/void ComputeInputPartialLeft(Matrix<ElemType>& inputGradientValues, const Matrix<ElemType>& gradientValues, const Matrix<ElemType>& leftMinusRight)  
        {
            inputGradientValues.AddWithScaleOf(gradientValues.Get00Element(), leftMinusRight);
        }

        /*TODO: merge with call site*/void ComputeInputPartialRight(Matrix<ElemType>& inputGradientValues, const Matrix<ElemType>& gradientValues, const Matrix<ElemType>& leftMinusRight)  
        {
            inputGradientValues.AddWithScaleOf(-gradientValues.Get00Element(), leftMinusRight);
        }

        virtual void /*ComputationNodeNonLooping::*/EvaluateThisNodeNonLooping() override
        {
            EvaluateThisNodeS(FunctionValues(), Inputs(0)->FunctionValues(), Inputs(1)->FunctionValues(), m_leftMinusRight, shared_from_this());
        }

        /*TODO: merge with call site*/void EvaluateThisNodeS(Matrix<ElemType>& functionValues, const Matrix<ElemType>& inputFunctionValues0, const Matrix<ElemType>& inputFunctionValues1, Matrix<ElemType>& leftMinusRight, ComputationNodePtr curNode)  
        {
            leftMinusRight.AssignDifferenceOf(inputFunctionValues0, inputFunctionValues1);
            curNode->MaskMissingColumnsToZero(leftMinusRight, Inputs(0)->GetMBLayout());    // we are fine since it will only be called with full minibatch.
            ElemType v = leftMinusRight.FrobeniusNorm();
            VerifySize(1,1);
            functionValues.SetValue(v*v/2);
#if NANCHECK
            functionValues.HasNan("SquareError");
#endif
        }

        virtual void /*ComputationNodeBase::*/Validate(bool isFinalValidationPass) override
        {
            ValidateBinaryReduce(isFinalValidationPass);
            m_leftMinusRight.Resize(Inputs(0)->GetNumRows(), Inputs(0)->GetNumCols());
        }

        virtual void InferImageDimsFromInputs()
        {
            InferImageDimsFromInput(0, false);

            m_outputImageLayout = ImageLayout();
        }       

        virtual void MoveMatricesToDevice(const DEVICEID_TYPE deviceId)
        {
            Base::MoveMatricesToDevice(deviceId);
            m_leftMinusRight.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
        }

        virtual void CopyTo(const ComputationNodePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const
        {
            Base::CopyTo(nodeP, newName, flags);
            if (flags & CopyNodeFlags::copyNodeValue)
            {
                auto node = dynamic_pointer_cast<SquareErrorNode<ElemType>>(nodeP);
                node->m_leftMinusRight = m_leftMinusRight;
            }
        }
    protected:
        virtual bool NodeDoesItsOwnCustomizedMissingColumnsMasking() { return true; }
    private:
        Matrix<ElemType> m_leftMinusRight;
    };

    template class SquareErrorNode<float>; 
    template class SquareErrorNode<double>;

    // -----------------------------------------------------------------------
    // CrossEntropyWithSoftmaxNode (labels, prediction)
    // -----------------------------------------------------------------------

    //calculates: -sum(left_i * log(softmax_i(right)))
    template<class ElemType>
    class CrossEntropyWithSoftmaxNode : public ComputationNodeNonLooping/*ComputationNode*/<ElemType>, public NumInputs<2>
    {
        typedef ComputationNodeNonLooping<ElemType> Base; UsingComputationNodeMembersBoilerplate;
        static const std::wstring TypeName() { return L"CrossEntropyWithSoftmax"; }
    public:
        CrossEntropyWithSoftmaxNode(DEVICEID_TYPE deviceId, const wstring & name) :
            Base(deviceId, name),
            m_logSoftmaxOfRight(deviceId), m_softmaxOfRight(deviceId)
        { }

        virtual void ComputeInputPartial(const size_t inputIndex)
        {
            //left Node must be a scalar
            if (inputIndex == 0)  //left derivative
            {
                ComputeInputPartialLeft(m_logSoftmaxOfRight, Inputs(inputIndex)->GradientValues(), GradientValues());
            }
            else
            {
                ComputeInputPartialRight(m_softmaxOfRight, Inputs(0)->FunctionValues(), Inputs(inputIndex)->GradientValues(), GradientValues());
                Inputs(inputIndex)->MaskMissingGradientColumnsToZero();
            }
        }

        /*TODO: merge with call site*/void ComputeInputPartialLeft(const Matrix<ElemType>& logSoftmaxOfRight, Matrix<ElemType>& inputGradientValues, 
            const Matrix<ElemType>& gradientValues)  
        {
#if DUMPOUTPUT
            logSoftmaxOfRight.Print("CrossEntropyWithSoftmax Partial-logSoftmaxOfRight");
            gradientValues.Print("CrossEntropyWithSoftmax Partial-gradientValues");
            inputGradientValues.Print("CrossEntropyWithSoftmaxNode Partial-Left-in");
#endif

            Matrix<ElemType>::ScaleAndAdd(-gradientValues.Get00Element(), logSoftmaxOfRight, inputGradientValues);
#if DUMPOUTPUT
            inputGradientValues.Print("CrossEntropyWithSoftmaxNode Partial-Left-out");
#endif

        }

        /*TODO: merge with call site*/void ComputeInputPartialRight(const Matrix<ElemType>& softmaxOfRight, const Matrix<ElemType>& inputFunctionValues, 
            Matrix<ElemType>& inputGradientValues, const Matrix<ElemType>& gradientValues)  
        {
#if DUMPOUTPUT
            softmaxOfRight.Print("CrossEntropyWithSoftmax Partial-softmaxOfRight");
            inputFunctionValues.Print("CrossEntropyWithSoftmax Partial-inputFunctionValues");
            gradientValues.Print("CrossEntropyWithSoftmax Partial-gradientValues");
            inputGradientValues.Print("CrossEntropyWithSoftmaxNode Partial-Right-in");
#endif

            Matrix<ElemType>::AddScaledDifference(gradientValues, softmaxOfRight, inputFunctionValues, inputGradientValues);
#if DUMPOUTPUT
            inputGradientValues.Print("CrossEntropyWithSoftmaxNode Partial-Right");
#endif
        }

        virtual void /*ComputationNodeNonLooping::*/EvaluateThisNodeNonLooping() override   //-sum(left_i * log(softmax_i(right)))
        {
            EvaluateThisNodeS(FunctionValues(), Inputs(0)->FunctionValues(), Inputs(1)->FunctionValues(), m_softmaxOfRight, m_logSoftmaxOfRight, shared_from_this());
        }

        /*TODO: merge with call site*/void EvaluateThisNodeS(Matrix<ElemType>& functionValues, const Matrix<ElemType>& inputFunctionValues0, const Matrix<ElemType>& inputFunctionValues1, 
            Matrix<ElemType>& softmaxOfRight, Matrix<ElemType>& logSoftmaxOfRight, ComputationNodePtr curNode)
        {
            logSoftmaxOfRight.AssignLogSoftmaxOf(inputFunctionValues1, true);
            softmaxOfRight.SetValue(logSoftmaxOfRight);
            softmaxOfRight.InplaceExp();
            curNode->MaskMissingColumnsToZero(logSoftmaxOfRight, Inputs(1)->GetMBLayout());     // we are fine here since it will be called only with full minibatch
            functionValues.AssignInnerProductOfMatrices(inputFunctionValues0, logSoftmaxOfRight);
            functionValues*=(-1);
#if NANCHECK
            functionValues.HasNan("CrossEntropyWithSoftmax");
#endif
#if DUMPOUTPUT
            functionValues.Print("CrossEntropyWithSoftmaxNode");
#endif
        }

        virtual void /*ComputationNodeBase::*/Validate(bool isFinalValidationPass) override
        {
            ValidateBinaryReduce(isFinalValidationPass);
            m_logSoftmaxOfRight.Resize(Inputs(0)->GetNumRows(), Inputs(0)->GetNumCols());
            m_softmaxOfRight.Resize(Inputs(0)->GetNumRows(), Inputs(0)->GetNumCols());
        }

        virtual void InferImageDimsFromInputs()
        {
            InferImageDimsFromInput(0, false);

            m_outputImageLayout = ImageLayout();
        }

        virtual void MoveMatricesToDevice(const DEVICEID_TYPE deviceId)
        {
            Base::MoveMatricesToDevice(deviceId);
            m_logSoftmaxOfRight.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
            m_softmaxOfRight.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
        }

        virtual void CopyTo(const ComputationNodePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const
        {
            Base::CopyTo(nodeP, newName, flags);
            if (flags & CopyNodeFlags::copyNodeValue)
            {
                auto node = dynamic_pointer_cast<CrossEntropyWithSoftmaxNode<ElemType>>(nodeP);
                node->m_logSoftmaxOfRight = m_logSoftmaxOfRight;
                node->m_softmaxOfRight = m_softmaxOfRight;
            }
        }
    protected:
        virtual bool NodeDoesItsOwnCustomizedMissingColumnsMasking() { return true; }
    protected:
        Matrix<ElemType> m_logSoftmaxOfRight;
        Matrix<ElemType> m_softmaxOfRight;       
    };

    template class CrossEntropyWithSoftmaxNode<float>; 
    template class CrossEntropyWithSoftmaxNode<double>;

    // -----------------------------------------------------------------------
    /// CrossEntropyNode (labels, prediction)
    // -----------------------------------------------------------------------

    // calculates: -sum(left_i * log(right_i))
    // assume softmax is already done
    // You probably want to use CrossEntropyWithSoftMaxNode instead, it is more efficient in most cases.
    template<class ElemType>
    class CrossEntropyNode : public ComputationNodeNonLooping/*ComputationNode*/<ElemType>, public NumInputs<2>
    {
        typedef ComputationNodeNonLooping<ElemType> Base; UsingComputationNodeMembersBoilerplate;
        static const std::wstring TypeName() { return L"CrossEntropy"; }
    public:
        CrossEntropyNode(DEVICEID_TYPE deviceId, const wstring & name) :
            Base(deviceId, name),
            m_logOfRight(deviceId), m_leftDivRight(deviceId)
        { }

        virtual void ComputeInputPartial(const size_t inputIndex)
        {
            //left Node must be a scalar
            if (inputIndex == 0)  //left derivative
            {
                ComputeInputPartialLeft(m_logOfRight, Inputs(inputIndex)->GradientValues(), GradientValues());
            }
            else
            {
                ComputeInputPartialRight(m_leftDivRight, Inputs(0)->FunctionValues(), Inputs(1)->FunctionValues(), Inputs(inputIndex)->GradientValues(), GradientValues(), shared_from_this());
            }
        }

        /*TODO: merge with call site*/void ComputeInputPartialLeft(const Matrix<ElemType>& logOfRight, Matrix<ElemType>& inputGradientValues, 
            const Matrix<ElemType>& gradientValues)  
        {
            Matrix<ElemType>::ScaleAndAdd(-gradientValues.Get00Element(), logOfRight, inputGradientValues);
        }

        /*TODO: merge with call site*/void ComputeInputPartialRight(Matrix<ElemType>& leftDivRight, 
            const Matrix<ElemType>& inputFunctionValues0, const Matrix<ElemType>& inputFunctionValues1,
            Matrix<ElemType>& inputGradientValues, const Matrix<ElemType>& gradientValues, ComputationNodePtr curNode)
        {
            leftDivRight.AssignElementDivisionOf(inputFunctionValues0, inputFunctionValues1);
            curNode->MaskMissingColumnsToZero(leftDivRight, Inputs(0)->GetMBLayout());
            Matrix<ElemType>::ScaleAndAdd(-gradientValues.Get00Element(), leftDivRight, inputGradientValues);
        }

        //-sum(left_i * log(right_i))
        virtual void /*ComputationNodeNonLooping::*/EvaluateThisNodeNonLooping() override
        {
            EvaluateThisNodeS(FunctionValues(), Inputs(0)->FunctionValues(), Inputs(1)->FunctionValues(), m_logOfRight, shared_from_this());
        }

        /*TODO: merge with call site*/void EvaluateThisNodeS(Matrix<ElemType>& functionValues, const Matrix<ElemType>& inputFunctionValues0, const Matrix<ElemType>& inputFunctionValues1, 
            Matrix<ElemType>& logOfRight, ComputationNodePtr curNode)
        {
            logOfRight.SetValue(inputFunctionValues1);
            logOfRight.InplaceLog();
            curNode->MaskMissingColumnsToZero(logOfRight, Inputs(1)->GetMBLayout());
            functionValues.AssignInnerProductOfMatrices(inputFunctionValues0, logOfRight);
            functionValues*=(-1);
#if NANCHECK
            functionValues.HasNan("CrossEntropy");
#endif
        }

        virtual void /*ComputationNodeBase::*/Validate(bool isFinalValidationPass) override
        {
            ValidateBinaryReduce(isFinalValidationPass);
            if (Inputs(0)->OperationName() != L"InputValue")    // TODO: but labels could be post-processed, e.g. sub-sampled. This test should not be here.
                LogicError("CrossEntropyNode criterion requires the first input to be the label.");
            m_logOfRight.Resize(Inputs(1)->GetNumRows(), Inputs(1)->GetNumCols());
            m_leftDivRight.Resize(Inputs(1)->GetNumRows(), Inputs(1)->GetNumCols());
        }

        virtual void InferImageDimsFromInputs()
        {
            InferImageDimsFromInput(0, false);

            m_outputImageLayout = ImageLayout();
        }

        virtual void MoveMatricesToDevice(const DEVICEID_TYPE deviceId)
        {
            Base::MoveMatricesToDevice(deviceId);
            m_logOfRight.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
            m_leftDivRight.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
        }

        virtual void CopyTo(const ComputationNodePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const
        {
            Base::CopyTo(nodeP, newName, flags);
            if (flags & CopyNodeFlags::copyNodeValue)
            {
                auto node = dynamic_pointer_cast<CrossEntropyNode<ElemType>>(nodeP);
                node->m_logOfRight = m_logOfRight;
                node->m_leftDivRight = m_leftDivRight;
            }
        }
    protected:
        virtual bool NodeDoesItsOwnCustomizedMissingColumnsMasking() { return true; }
    private:
        // matrix value passed from evaluate to computePartial
        Matrix<ElemType> m_logOfRight;
        // temporary
        Matrix<ElemType> m_leftDivRight;
    };

    template class CrossEntropyNode<float>; 
    template class CrossEntropyNode<double>;

    // -----------------------------------------------------------------------
    // MatrixL1RegNode (input)
    // TODO: share most code with MatrixL2RegNode
    // -----------------------------------------------------------------------

    template<class ElemType>
    class MatrixL1RegNode : public ComputationNodeNonLooping/*ComputationNode*/<ElemType>, public NumInputs<1>
    {
        typedef ComputationNodeNonLooping<ElemType> Base; UsingComputationNodeMembersBoilerplate;
        static const std::wstring TypeName() { return L"MatrixL1Reg"; }
    public:
        MatrixL1RegNode(DEVICEID_TYPE deviceId, const wstring & name) :
            Base(deviceId, name),
            m_gradientOfL1Norm(deviceId)
        { }

        virtual void ComputeInputPartial(const size_t inputIndex) // scale by number of cols (or samples)
        {
            assert(inputIndex == 0); inputIndex;
            ComputeInputPartialS(m_gradientOfL1Norm, Inputs(0)->GradientValues(), GradientValues(), Inputs(0)->FunctionValues());
        }

        /*TODO: merge with call site*/void ComputeInputPartialS(Matrix<ElemType>& gradientOfL1Norm, 
            Matrix<ElemType>& inputGradientValues, const Matrix<ElemType>& gradientValues, const Matrix<ElemType>& inputFunctionValues)  
        {
            gradientOfL1Norm.AssignSignOf(inputFunctionValues);
            inputGradientValues.AddWithScaleOf(gradientValues.Get00Element(), gradientOfL1Norm);
        }

        virtual void /*ComputationNodeNonLooping::*/EvaluateThisNodeNonLooping() override  
        {
            Inputs(0)->MaskMissingValuesColumnsToZero();
            VerifySize(1, 1);
            FunctionValues().SetValue(Inputs(0)->FunctionValues().MatrixNorm1());
#if NANCHECK
            FunctionValues().HasNan("MatrixL1Reg");
#endif
        }

        virtual void /*ComputationNodeBase::*/Validate(bool isFinalValidationPass) override
        {
            ValidateUnaryReduce(isFinalValidationPass);
            m_gradientOfL1Norm.Resize(Inputs(0)->GetNumRows(), Inputs(0)->GetNumCols());
        }

        virtual void InferImageDimsFromInputs()
        {
            InferImageDimsFromInput(0, false);

            m_outputImageLayout = ImageLayout();
        }

        virtual void MoveMatricesToDevice(const DEVICEID_TYPE deviceId)
        {
            Base::MoveMatricesToDevice(deviceId);
            m_gradientOfL1Norm.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
        }

        virtual void CopyTo(const ComputationNodePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const
        {
            Base::CopyTo(nodeP, newName, flags);
            if (flags & CopyNodeFlags::copyNodeValue)
            {
                auto node = dynamic_pointer_cast<MatrixL1RegNode<ElemType>>(nodeP);
                node->m_gradientOfL1Norm = m_gradientOfL1Norm;
            }
        }
    protected:
        virtual bool NodeDoesItsOwnCustomizedMissingColumnsMasking() { return true; }
    private:
        Matrix<ElemType> m_gradientOfL1Norm;    // temporary
    };

    template class MatrixL1RegNode<float>; 
    template class MatrixL1RegNode<double>;

    // -----------------------------------------------------------------------
    // MatrixL2RegNode (input)
    // TODO: share most code with MatrixL1RegNode
    // -----------------------------------------------------------------------

    template<class ElemType>
    class MatrixL2RegNode : public ComputationNodeNonLooping/*ComputationNode*/<ElemType>, public NumInputs<1>
    {
        typedef ComputationNodeNonLooping<ElemType> Base; UsingComputationNodeMembersBoilerplate;
        static const std::wstring TypeName() { return L"MatrixL2Reg"; }
    public:
        MatrixL2RegNode(DEVICEID_TYPE deviceId, const wstring & name) :
            Base(deviceId, name),
            m_temp(deviceId)
        { }

        virtual void ComputeInputPartial(const size_t inputIndex) // scale by number of cols (or samples)
        {
            assert(inputIndex == 0); inputIndex;
            ComputeInputPartialS(Inputs(0)->GradientValues(), GradientValues(), Inputs(0)->FunctionValues(), FunctionValues());
        }

        /*TODO: merge with call site*/void ComputeInputPartialS(Matrix<ElemType>& inputGradientValues, const Matrix<ElemType>& gradientValues, const Matrix<ElemType>& inputFunctionValues, const Matrix<ElemType>& functionValues)  
        {
            ElemType v = gradientValues.Get00Element() / (functionValues.Get00Element() + EPS_IN_INVERSE);
            inputGradientValues.AddWithScaleOf(v, inputFunctionValues);
        }

        virtual void /*ComputationNodeNonLooping::*/EvaluateThisNodeNonLooping() override  
        {
            Inputs(0)->MaskMissingValuesColumnsToZero();
            VerifySize(1,1);
            FunctionValues().SetValue(Inputs(0)->FunctionValues().FrobeniusNorm());
#if NANCHECK
            FunctionValues().HasNan("MatrixL2Reg");
#endif
        }

        virtual void /*ComputationNodeBase::*/Validate(bool isFinalValidationPass) override
        {
            ValidateUnaryReduce(isFinalValidationPass);
        }

        virtual void InferImageDimsFromInputs()
        {
            InferImageDimsFromInput(0, false);

            m_outputImageLayout = ImageLayout();
        }

        virtual void MoveMatricesToDevice(const DEVICEID_TYPE deviceId)
        {
            Base::MoveMatricesToDevice(deviceId);
            m_temp.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
        }
    protected:
        virtual bool NodeDoesItsOwnCustomizedMissingColumnsMasking() { return true; }
    private:
        Matrix<ElemType> m_temp;
    };

    template class MatrixL2RegNode<float>; 
    template class MatrixL2RegNode<double>;

    // -----------------------------------------------------------------------
    /// NoiseContrastiveEstimationNode (labels, input, inputWeights, biasWeights)
    // -----------------------------------------------------------------------

    enum NCEEvalMode
    {
        Softmax = 0,
        Unnormalized = 1,
        None = 2
    };
    template<class ElemType>
    class NoiseContrastiveEstimationNode : public ComputationNodeNonLooping/*ComputationNode*/<ElemType>, public NumInputs<4>
    {
        typedef ComputationNodeNonLooping<ElemType> Base; UsingComputationNodeMembersBoilerplate;
        static const std::wstring TypeName() { return L"NCEBasedCrossEntropyWithSoftmax"; }
    public:
        NoiseContrastiveEstimationNode(DEVICEID_TYPE deviceId, const wstring & name) :
            Base(deviceId, name),
            m_logSoftmax(deviceId),
            m_softMax(deviceId), m_grdToSoftMaxInput(deviceId), m_ncePrediction(deviceId),
            m_evalMode(NCEEvalMode::None)
        { }
        NoiseContrastiveEstimationNode(DEVICEID_TYPE deviceId, const wstring & name, NCEEvalMode xm_evalMode) :
            Base(deviceId, name),
            m_logSoftmax(deviceId),
            m_softMax(deviceId), m_grdToSoftMaxInput(deviceId), m_ncePrediction(deviceId),
            m_evalMode(xm_evalMode)
        { }
        // ^^ TODO: we can merge these two

        virtual void SaveToFile(File& fstream) const override
        {
            Base::SaveToFile(fstream);
            fstream << m_evalMode;
        }

        virtual void LoadFromFile(File& fstream, size_t modelVersion) override
        {
            Base::LoadFromFile(fstream, modelVersion);
            fstream >> m_evalMode;
            if (m_evalMode > NCEEvalMode::None)
            {
                m_evalMode = NCEEvalMode::None;
                fstream.SetPosition(fstream.GetPosition() - sizeof(m_evalMode));
            }
        }

        void SetEvalMode(NCEEvalMode& xevMode) { m_evalMode = xevMode; }
        NCEEvalMode & EvalMode() { return m_evalMode; } // TODO: really? Return a reference to a local? TODO: change to const? and call it GetEvalMode()

        /**
        compute gradients to input observations, the weights to the observations, and the class log posterior probabilities
        */
        virtual void ComputeInputPartial(const size_t inputIndex)
        {
            m_needRecomputeGradientToSoftmaxInput = false;
            //gradient computation@yinggongzhao
            //inputIndex should be 2 this time
            if (m_evalMode != NCEEvalMode::None)
                LogicError("ComputeInputPartial should only be called in training mode");
            if (inputIndex == 0)
                InvalidArgument("ComputeInput partial should not be called for label");
            //                                                                              samples+probs                   hidden                  embedding
            Inputs(inputIndex)->GradientValues().AssignNCEDerivative(m_ncePrediction, Inputs(0)->FunctionValues(), Inputs(1)->FunctionValues(), Inputs(2)->FunctionValues(), inputIndex);
        }

        /*TODO: merge with call site*/void ComputeInputPartialRight(const Matrix<ElemType>& inputFunctionValues, Matrix<ElemType>& inputGradientValues, const Matrix<ElemType>& gradientValues)
        {
            Matrix<ElemType>::MultiplyAndAdd(inputFunctionValues, false, gradientValues, true, inputGradientValues);
        }

        /*TODO: merge with call site*/void ComputeInputPartialLeft(const Matrix<ElemType>& obs, Matrix<ElemType>& inputGradientValues, const Matrix<ElemType>& gradientValues)
        {
            Matrix<ElemType>::MultiplyAndAdd(obs, false, gradientValues, false, inputGradientValues);
        }

        static void WINAPI ComputeCEPartialToSoftmaxInputs(Matrix<ElemType>& inputGradientValues, Matrix<ElemType>& gradientValues, size_t y_t)
        {
            Matrix<ElemType>::MinusOneAt(inputGradientValues, y_t);
            Matrix<ElemType>::Scale(gradientValues, inputGradientValues);
        }

        virtual void /*ComputationNodeNonLooping::*/EvaluateThisNodeNonLooping() override   //-sum(left_i * log(softmax_i(right)))
        {
            int positive = 0, negative = 0;
            if (Inputs(0)->GetNumRows() == 1)
            {
                for (int i = 0; i < Inputs(0)->GetNumCols(); i++)
                {
                    if (Inputs(0)->FunctionValues()(0, i) > 0)
                        positive++;
                    else if (Inputs(0)->FunctionValues()(0, i) < 0)
                        negative++;
                }
                assert(positive * negative == 0);
            }
            if (m_evalMode == NCEEvalMode::Softmax || (Inputs(0)->GetNumRows() == 1 && positive > 0))
            {
                // evaluation uses softmax
                m_logSoftmax.AssignProductOf(Inputs(1)->FunctionValues(), true, Inputs(2)->FunctionValues(), false);
                m_logSoftmax += Inputs(3)->FunctionValues();
                m_logSoftmax.InplaceLogSoftmax(false);
                FunctionValues().AssignSoftmaxSum(Inputs(0)->FunctionValues(), m_logSoftmax);
            }
            else if (m_evalMode == NCEEvalMode::Unnormalized || (Inputs(0)->GetNumRows() == 1 && negative > 0))
            {
                FunctionValues().AssignNceUnnormalizedEval(Inputs(0)->FunctionValues(), Inputs(1)->FunctionValues(), Inputs(2)->FunctionValues(), Inputs(3)->FunctionValues());
            }
            else
            {
                // training criterion uses NCE
                //likelihood                                         samples+probs                        hidden                       embedding            bias
                FunctionValues().AssignNoiseContrastiveEstimation(Inputs(0)->FunctionValues(), Inputs(1)->FunctionValues(), Inputs(2)->FunctionValues(), Inputs(3)->FunctionValues(), m_ncePrediction);
            }
            m_needRecomputeGradientToSoftmaxInput = true;
        }

        /**
        Inputs: [0] label in dense matrix in [4 x T]
        the first row is the word index, the second row is the class index, the third row is the first word index of the class
        the last row is the first word index of the next class
        [1] hidden layer activity to the node in [hdsize x T]. for a simple rnn, this is the hidden layer activty
        [2] weight matrix in [hdsize x vocab_size], for speed-up, as per word matrix can be simply obtained as column slice
        [3] clsprob in dense matrix in [nbr_cls x T]. this is the output from logsoftmax node for the log-posterior probabilty of class given observations
        */
        virtual void /*ComputationNodeBase::*/Validate(bool isFinalValidationPass) override
        {
            Base::Validate(isFinalValidationPass);

            if (Inputs(0)->OperationName() != OperationNameOf(InputValue))
                LogicError("NoiseContrastiveEstimationNode criterion requires the first input to be the label.");
            if (isFinalValidationPass)
            {
                if (!(Inputs(1)->GetNumRows() == Inputs(2)->GetNumRows())) // input and matrix can be timed
                    LogicError("The Matrix dimension for observation and weight in the NoiseContrastiveEstimationNode operation does not match.");
                if (!(Inputs(0)->GetNumCols() == Inputs(1)->GetNumCols())) // label and input same obs numbers
                    LogicError("The Matrix dimension for label and observation in the NoiseContrastiveEstimationNode operation does not match.");
            }

            //cerr << Inputs(3)->GetNumCols() << "\t" << Inputs(0)->GetNumCols() << endl;
            Resize(1,1);
            m_pMBLayout = nullptr;    // this node does not hold mini-batch data
            InferImageDimsFromInputs();
        }

        virtual void InferImageDimsFromInputs()
        {
            InferImageDimsFromInput(0, false);
            m_outputImageLayout = ImageLayout();
        }

        virtual void MoveMatricesToDevice(const DEVICEID_TYPE deviceId)
        {
            Base::MoveMatricesToDevice(deviceId);
            m_logSoftmax.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
            m_softMax.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
            m_grdToSoftMaxInput.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
        }
    protected:
        virtual bool NodeDoesItsOwnCustomizedMissingColumnsMasking() { return true; }
    protected:
        Matrix<ElemType> m_logSoftmax;
        Matrix<ElemType> m_softMax;
        Matrix<ElemType> m_ncePrediction;

        // gradient of cross entropy with respect to the input of softmax
        // a 1 row by \sum_t m_nbrWordsInEachTime[t] vector
        // one slice of size m_nbrWordsInEachTime[t] saves the input to softmax for word y_t
        Matrix<ElemType> m_grdToSoftMaxInput;
        bool m_needRecomputeGradientToSoftmaxInput;

        size_t m_nbrNoise;
        size_t           m_totalNbrWords;
    private:
        NCEEvalMode m_evalMode;
    };
    template class NoiseContrastiveEstimationNode<float>;
    template class NoiseContrastiveEstimationNode<double>;

    // -----------------------------------------------------------------------
    /// ClassBasedCrossEntropyWithSoftmaxNode (labels(.,t), input(.,t), inputweights, clsProbBeforeSoftmax(.,t))
    // Inputs:
    // Inputs(0) [4 x T] label in dense matrix in
    //           (0,t) the first row is the word index
    //           (1,t) the second row is the class index
    //           (2,t) the third row is the first word index of the class
    //           (3,t) the last row is the first word index of the next class
    // Inputs(1) [hdsize x T] hidden layer activation to the node in. for a simple rnn, this is the hidden layer activty
    // Inputs(2) [hdsize x vocab_size] weight matrix in, for speed-up, as per word matrix can be simply obtained as column slice
    // Inputs(3) [nbr_cls x T] clsprob in dense matrix in. this input, if applied softmax on, is the posterior probabilty of class given observations
    // -----------------------------------------------------------------------

    // calculates: -sum(left_i * log(softmax_i(right))) for class given history and for word given history
    // need to provide class probabilty from external node
    template<class ElemType>
    class ClassBasedCrossEntropyWithSoftmaxNode : public ComputationNodeNonLooping/*ComputationNode*/<ElemType>, public NumInputs<4>
    {
        typedef ComputationNodeNonLooping<ElemType> Base; UsingComputationNodeMembersBoilerplate;
        static const std::wstring TypeName() { return L"ClassBasedCrossEntropyWithSoftmax"; }
    public:
        ClassBasedCrossEntropyWithSoftmaxNode(DEVICEID_TYPE deviceId, const wstring & name) :
            Base(deviceId, name),
            m_logSoftmax(deviceId), m_softMax(deviceId), m_grdToSoftMaxInput(deviceId), m_clsLogSoftmax(deviceId), m_clsSoftmax(deviceId)
        { }

        /**
        compute gradients to input observations, the weights to the observations, and the class log posterior probabilites
        */
        virtual void ComputeInputPartial(const size_t inputIndex) override
        {
            // this should never be called for input[0], which is controlled through the needGradient flag
            if (inputIndex != 1 && inputIndex != 2 && inputIndex != 3)
                InvalidArgument("ClassCrossEntropyWithSoftmaxNode criterion only takes with respect to input, weight to the input and class log posterior probability.");

            ComputeSoftMaxPartial();

            Matrix<ElemType> grd_t;
            Matrix<ElemType> grd_to_wgt_t;

            const size_t nT = Inputs(0)->GetNumTimeSteps();
            const size_t nS = Inputs(0)->GetNumParallelSequences();
            size_t sz = 0;     // iterate over the packed concatenated class-conditioned prob vectors
            for (size_t s = 0; s < nS; s++) for (size_t t = 0; t < nT; t++)
            {
                if (Inputs(0)->GetMBLayout()->Is(s, t, MinibatchPackingFlags::NoInput))  // skip gaps
                    continue;
                FrameRange frameRange = FrameRange(t).Sequence(s);

                Matrix<ElemType> lbl_t = Inputs(0)->ValueSlice(frameRange);
                size_t c_t = (size_t)lbl_t(1, 0);
                size_t lft_bnd = (size_t)lbl_t(2, 0); // index of first word belonging to current word token's class
                size_t rgt_bnd = (size_t)lbl_t(3, 0); // and end of that range
                size_t nbr_wrd = (rgt_bnd - lft_bnd); // number of words in the class

                // compute prb - 1 and prb
                Matrix<ElemType> weightForClass = Inputs(2)->FunctionValues().ColumnSlice(lft_bnd, nbr_wrd);
                Matrix<ElemType> obs = Inputs(1)->ValueSlice(frameRange);   // hidden activation vector for current word token
                Matrix<ElemType> grd_to_soft_max_input = m_grdToSoftMaxInput.ColumnSlice(sz, nbr_wrd);
                Matrix<ElemType> grd_to_cls_prob = DataSlice(m_clsLogSoftmax, frameRange, Inputs(3)->GetMBLayout());

                switch (inputIndex)
                {
                case 1:
                    // gradient to input
                    grd_t = Inputs(1)->GradientSlice(frameRange);
                    Matrix<ElemType>::MultiplyAndAdd(weightForClass, false, grd_to_soft_max_input, true, grd_t);
                    break;
                case 2:
                    // gradient to input weight
                    grd_to_wgt_t = Inputs(2)->GradientValues().ColumnSlice(lft_bnd, nbr_wrd);
                    Matrix<ElemType>::MultiplyAndAdd(obs, false, grd_to_soft_max_input, false, grd_to_wgt_t);
                    break;
                case 3:
                    grd_t = Inputs(3)->GradientSlice(frameRange);
                    grd_t.SetValue(DataSlice(m_clsSoftmax, frameRange, Inputs(3)->GetMBLayout()));
                    ComputeCEPartialToSoftmaxInputs(grd_t, GradientValues(), c_t);
                    break;
                }

                sz += nbr_wrd;
            }
        }
    private:
        void ComputeCEPartialToSoftmaxInputs(Matrix<ElemType>& inputGradientValues, Matrix<ElemType>& gradientValues, size_t y_t)
        {
            Matrix<ElemType>::MinusOneAt(inputGradientValues, y_t);
            Matrix<ElemType>::Scale(gradientValues, inputGradientValues);
        }

        // gradient of cross entropy w.r.t. to input to softmax
        void ComputeSoftMaxPartial()
        {
            if (m_needRecomputeGradientToSoftmaxInput)
            {
                m_grdToSoftMaxInput.Resize(1, m_totalNbrWords); // buffer that contains a concatenation of class-conditional values

                const size_t nT = Inputs(0)->GetNumTimeSteps();
                const size_t nS = Inputs(0)->GetNumParallelSequences();
                size_t sz = 0;     // iterate over the packed concatenated class-conditioned prob vectors
                for (size_t s = 0; s < nS; s++) for (size_t t = 0; t < nT; t++)
                {
                    if (Inputs(0)->GetMBLayout()->Is(s, t, MinibatchPackingFlags::NoInput))  // skip gaps
                        continue;
                    FrameRange frameRange = FrameRange(t).Sequence(s);

                    Matrix<ElemType> lbl_t = Inputs(0)->ValueSlice(frameRange);
                    size_t y_t = (size_t)lbl_t(0, 0);       // word index
                    size_t lft_bnd = (size_t)lbl_t(2, 0);   // index of first word belonging to current word token's class
                    size_t rgt_bnd = (size_t)lbl_t(3, 0);   // and end of that range
                    size_t nbr_wrd = (rgt_bnd - lft_bnd);   // number of words in the class

                    Matrix<ElemType> softMax = m_softMax.ColumnSlice(sz, nbr_wrd);

                    size_t idx_in_class = y_t - lft_bnd;
                    ComputeCEPartialToSoftmaxInputs(softMax, GradientValues(), idx_in_class);

                    m_grdToSoftMaxInput.ColumnSlice(sz, nbr_wrd).SetValue(softMax);

                    sz += nbr_wrd;
                }

                m_needRecomputeGradientToSoftmaxInput = false;
            }
        }
    public:

        // -sum(left_i * log(softmax_i(right)))
        virtual void /*ComputationNodeNonLooping::*/EvaluateThisNodeNonLooping() override
        {
            if (Inputs(0)->FunctionValues().GetDeviceId() != CPUDEVICE)
                LogicError("ClassBasedCrossEntropyWithSoftmax (EvaluateThisNodeNonLooping()): The label matrix is not using CPU device. This will make computation slow, even though the label data is probably saved on GPU. Because of the external loop over time with explicit class id retrieved from the label matrix, the computation will be very slow if the label matrix is saved on GPU. However, this is only a constraint for label matrix and other matrices such as data are suggested to reside on GPU. ");

            // (the below is left-over from refactoring)
            Matrix<ElemType>& functionValues = FunctionValues();
            
            const size_t hdSize = Inputs(1)->GetNumRows();    // hdSize
            assert(m_nbrCls == Inputs(3)->GetNumRows());

            // compute the class posteriors
            m_clsLogSoftmax = Inputs(3)->FunctionValues();
            m_clsLogSoftmax.InplaceLogSoftmax(true);        // log
            m_clsSoftmax.AssignExpOf(m_clsLogSoftmax);      // non-log

            // create a large workspace to contain all class-conditioned probs concatenated
            // 'sz' is the offset into that vector. We will iterate over these vectors at a few places. Always use this same boilerplate code.
            // TODO: should we pull this iteration into an iterator, to reduce the code dup?
            const size_t nT = Inputs(0)->GetNumTimeSteps();
            const size_t nS = Inputs(0)->GetNumParallelSequences();
            size_t sz = 0;
            for (size_t s = 0; s < nS; s++) for (size_t t = 0; t < nT; t++)
            {
                if (Inputs(0)->GetMBLayout()->Is(s, t, MinibatchPackingFlags::NoInput))  // skip gaps
                    continue;
                FrameRange frameRange = FrameRange(t).Sequence(s);

                const Matrix<ElemType> & lbl_t = Inputs(0)->ValueSlice(frameRange);
                size_t lft_bnd = (size_t)lbl_t(2, 0);
                size_t rgt_bnd = (size_t)lbl_t(3, 0);
                size_t nbr_wrd = (rgt_bnd - lft_bnd);   // number of words in the class
                if (nbr_wrd == 0)
                    LogicError("ClassBasedCrossEntropyWithSoftmax (EvaluateThisNodeNonLooping()): Encountered a class of size 0. This sample seems to lack an NoInput flag.");

                sz += nbr_wrd;
            }
            m_totalNbrWords = sz;   // total size of concatenated vector

            // buffer to hold the concatenated class-conditioned prob vectors
            m_softMax.Resize(1, sz);
            m_logSoftmax.Resize(1, sz);

            // accumulate objective
            functionValues.SetValue(0);
            sz = 0;     // iterate over the packed concatenated class-conditioned prob vectors
            for (size_t s = 0; s < nS; s++) for (size_t t = 0; t < nT; t++)
            {
                if (Inputs(0)->GetMBLayout()->Is(s, t, MinibatchPackingFlags::NoInput))  // skip gaps
                    continue;
                FrameRange frameRange = FrameRange(t).Sequence(s);

                const Matrix<ElemType> & lbl_t = Inputs(0)->ValueSlice(frameRange);
                size_t y_t = (size_t)lbl_t(0, 0);     // current word token index
                size_t c_t = (size_t)lbl_t(1, 0);     // current word token's class index
                size_t lft_bnd = (size_t)lbl_t(2, 0); // index of first word belonging to current word token's class
                size_t rgt_bnd = (size_t)lbl_t(3, 0); // and end of that range
                size_t nbr_wrd = (rgt_bnd - lft_bnd);   // number of words in the class

                // now get views of various arrays that correspond to the index range of words belonging to this class

                // get hidden vectors for the words in this class
                Matrix<ElemType> weightForClass = Inputs(2)->FunctionValues().ColumnSlice(lft_bnd, nbr_wrd);    // [hdSize x nbr_wrd]

                // buffer to hold the class-conditional distribution
                Matrix<ElemType> softMax_t    =    m_softMax.ColumnSlice(sz, nbr_wrd);
                Matrix<ElemType> logSoftMax_t = m_logSoftmax.ColumnSlice(sz, nbr_wrd);

                Matrix<ElemType> obs = Inputs(1)->ValueSlice(frameRange);   // hidden activation vector for current word token

                // multiply hidden activation with weight matrix (the slice of the weight matrix for the range of class members)
                // TODO: can we use 'true' here instead? Above transposition hack won't work with row slices. 'obs' not used elsewhere
                obs.Reshape(1, hdSize);  // transpose it (make it a column vector)
                logSoftMax_t.AssignProductOf(obs/*(1 x hdSize)*/, false, weightForClass/*hdSize x nbr_wrd*/, false);  // -> 1 x nbr_word

                // log softmax(W x_t)
                logSoftMax_t.InplaceLogSoftmax(false);

                // and non-log version
                softMax_t.SetValue(logSoftMax_t);
                softMax_t.InplaceExp();
                // we now have a column vector of class-conditional probabilities over the class members

                // add  the word's class-conditional log posterior
                if (y_t < lft_bnd || y_t >= rgt_bnd)
                    LogicError("ClassBasedCrossEntropyWithSoftmax (EvaluateThisNodeNonLooping()): Word index out of bounds of class-member index range (word not a class member).");
                size_t idx_in_class = y_t - lft_bnd;
                Matrix<ElemType>::AddElementToElement(logSoftMax_t, 0, idx_in_class, functionValues, 0, 0);   // (1x1)

                // add the class log posterior probability
                Matrix<ElemType>::AddElementToElement(m_clsLogSoftmax, c_t, t, functionValues, 0, 0);     // (1x1)

                sz += nbr_wrd;
            }

            functionValues *= (-1);

#if NANCHECK
            functionValues.HasNan("ClassBasedCrossEntropyWithSoftmax");
#endif
            m_needRecomputeGradientToSoftmaxInput = true;
        }

#if 0   // no longer needed, using Base::MaskMissingColumnsToZero() instead
        /**
        reset to error signals to 0 for any elements without labels
        */
        // BUGBUG: the layout should be that of matrixToBeMasked, not of 'this'
        bool MaskOneMissingColumnToZero(Matrix<ElemType>& matrixToBeMasked, const size_t j) const
        {
            size_t nS = pMBLayout->GetNumParallelSequences();
            size_t t = j / nS;  // this is the time stamp
            size_t id = j % nS;  // this is the stream
            return Base::MaskMissingColumnsToZero(matrixToBeMasked, t, id);
#if 0       // old version prior to merging with Base version
            bool foundLabelOrFeatureMissing = false; /// set to true if either nolabel or feature missing is processed 

            if (!pMBLayout->IsAllNone())
            {
                if (pMBLayout->Is(t, MinibatchPackingFlags::NoLabel)) // TODO: this outer test is redundant here, no?
                {
                    if (pMBLayout->Is(id, t, MinibatchPackingFlags::NoLabel))
                    {
                        matrixToBeMasked.ColumnSlice(t * nS + id,1).SetValue(0);
                        foundLabelOrFeatureMissing = true;
                    }
                }
            }

            return foundLabelOrFeatureMissing;
#endif
        }
#endif

        virtual void /*ComputationNodeBase::*/Validate(bool isFinalValidationPass) override
        {
            Base::Validate(isFinalValidationPass);

            if (Inputs(0)->OperationName() != OperationNameOf(InputValue))  // TODO: but why could that label not be post-processed through another node?
                LogicError("ClassBasedCrossEntropyWithSoftmaxNode criterion requires the first input to be the label.");
            if (isFinalValidationPass)
            {
                if (Inputs(0)->GetNumRows() != 4) // label needs to be 4 rows
                    LogicError("The label in the ClassBasedCrossEntropyWithSoftmaxNode operation needs to be 4 rows.");
                if (Inputs(1)->GetNumRows() != Inputs(2)->GetNumRows()) // input and matrix can be timed
                    LogicError("The Matrix<ElemType>  dimension for observation and weight in the ClassBasedCrossEntropyWithSoftmaxNode operation does not match.");
                if (Inputs(0)->GetMBLayout() != Inputs(1)->GetMBLayout() || Inputs(0)->GetMBLayout() != Inputs(3)->GetMBLayout())
                    InvalidArgument("%ls %ls operation requires that the layouts of inputs 0 (label), 1 (hidden activation), and 3 (log softmax) match.", NodeName().c_str(), OperationName().c_str());
            }

            Resize(1, 1);
            m_pMBLayout = nullptr;    // this node does not hold mini-batch data
            InferImageDimsFromInputs();

            m_nbrCls = Inputs(3)->GetNumRows();
        }

        virtual void InferImageDimsFromInputs()
        {
            InferImageDimsFromInput(0, false);

            m_outputImageLayout = ImageLayout();
        }

        virtual void MoveMatricesToDevice(const DEVICEID_TYPE deviceId)
        {
            Base::MoveMatricesToDevice(deviceId);
            m_logSoftmax.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
            m_softMax.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
            m_clsLogSoftmax.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
            m_clsSoftmax.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
            m_grdToSoftMaxInput.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
        }
    protected:
        virtual bool NodeDoesItsOwnCustomizedMissingColumnsMasking() { return true; }
    protected:
        Matrix<ElemType> m_logSoftmax;
        Matrix<ElemType> m_softMax;

        Matrix<ElemType> m_clsLogSoftmax;
        Matrix<ElemType> m_clsSoftmax;

        /// gradient of cross entropy with respect to the input of softmax
        /// a 1 row by \sum_t m_nbrWordsInEachTime[t] vector
        /// one slice of size m_nbrWordsInEachTime[t] saves the input to softmax for word y_t
        Matrix<ElemType> m_grdToSoftMaxInput;
        bool m_needRecomputeGradientToSoftmaxInput;

        size_t           m_nbrCls;
        size_t           m_totalNbrWords;
    };

    template class ClassBasedCrossEntropyWithSoftmaxNode<float>;
    template class ClassBasedCrossEntropyWithSoftmaxNode<double>;

    // -----------------------------------------------------------------------
    // CRFNode (labels, position_dependent_scores, transition_scores)
    //  - labels : output label vector of [0:T-1]
    //  - position_dependent_scores [?] : score from position dependent node,
    //    in the R-CRF case, it is the RNN output score before softmax
    //  - transition scores [?] : score from the transition node, 
    //    in the R-CRF case, it is the transition probability between labels
    // -----------------------------------------------------------------------

    /**
        CRF training criterion 
        It uses forward-backward algorithm within a minibatch to compute statistics for sequence level optimization 
        This node can serve a base class for other sequence level optimization

        Developed by Kaisheng Yao
        This node is for replicating results of the following work
        K. Yao, B. Peng, G. Zweig, D. Yu, X. Li and F. Gao, "Recurrent Conditional Random Fields", NIPS Deep Learning Workshop 2014
        K. Yao, B. Peng, G. Zweig, D. Yu, X. Li and F. Gao, "Recurrent Conditional Random Fields for Language Understanding", ICASSP 2014 
        http://research.microsoft.com/pubs/210167/rcrf_v9.pdf

        The forward-backward algorithm follows the derivation in 
        http://jmlr.org/papers/volume12/collobert11a/collobert11a.pdf

    */
    template<class ElemType>
    class CRFNode : public ComputationNodeNonLooping/*ComputationNode*/<ElemType>, public NumInputs<3>
    {
        typedef ComputationNodeNonLooping<ElemType> Base; UsingComputationNodeMembersBoilerplate;
        static const std::wstring TypeName() { return L"CRF"; }
    public:
        CRFNode(DEVICEID_TYPE deviceId, const wstring & name) :
            Base(deviceId, name),
            mAlpha(deviceId), mBeta(deviceId), mPostProb(deviceId)
        { }

        /// compute posterior probability of label y at position t
        virtual void /*ComputationNodeNonLooping::*/EvaluateThisNodeNonLooping() override
        {
            size_t nrow = Inputs(0)->GetNumRows();
            size_t ncol = Inputs(0)->GetNumCols();

            mAlpha.Resize(nrow, ncol);
            mBeta.Resize(nrow, ncol);
            mPostProb.Resize(nrow, ncol);

            FunctionValues().SetValue(0.0);
            Matrix<ElemType> funcVal = FunctionValues();    // TODO: This just creates a 1x1 matrix set to 0.

            size_t nS = Inputs(0)->GetNumParallelSequences();
            if (nS != 1)
                LogicError("CRFNode: >1 parallel sequences are curently not implemented correctly. To fix this, we need Matrix::RowSlice(), which is a major change");
            for (size_t i = 0; i < nS; i++)     // process parallel sequences one by one
            {
                FrameRange sequenceRange = FrameRange().Sequence(i);    // FrameRange to select one sequence
                // BUGBUG: This ^^ is currently not supported. To implement it, we'd need Matrix::RowSlice().
                EvaluateThisNodeS(
                    DataSlice(mPostProb, sequenceRange, Inputs(0)->GetMBLayout()),
                    DataSlice(mAlpha,    sequenceRange, Inputs(0)->GetMBLayout()),
                    DataSlice(mBeta,     sequenceRange, Inputs(0)->GetMBLayout()),
                    funcVal,
                    Inputs(0)->ValueSlice(sequenceRange),
                    Inputs(1)->ValueSlice(sequenceRange),
                    Inputs(2)->FunctionValues(), mStartLbl,
                    mEndLbl);

                FunctionValues() += funcVal;    // aggregate over sequences
            }
        }

        virtual void ComputeInputPartial(const size_t inputIndex)  //scaled by 2*number of colmns (samples) in the Matrix<ElemType>
        {
            // inputIndex 0 should not get us here, it should be prevented by the needGradient flag of input[0]
            if (inputIndex != 1 && inputIndex != 2)
                InvalidArgument("CRFNode only takes with respect to input and weight.");

            if (inputIndex == 1)
                ErrorSignalToPostitionDependentNode(GradientValues(), Inputs(0)->FunctionValues(), mPostProb, Inputs(inputIndex)->GradientValues());
            else if (inputIndex == 2)
            {
                assert(Inputs(inputIndex)->GradientValues().GetNumElements() > 0);
                size_t nS = Inputs(0)->GetNumParallelSequences();
                for (size_t i = 0; i < nS; i++)         // process all sequences one by one
                {
                    FrameRange sequenceRange = FrameRange().Sequence(i);    // FrameRange to select one sequence
                    ErrorSignalToTransitionNode(
                        Inputs(0)->ValueSlice(sequenceRange),
                        DataSlice(mAlpha,     sequenceRange, Inputs(0)->GetMBLayout()),
                        DataSlice(mBeta,      sequenceRange, Inputs(0)->GetMBLayout()),
                        Inputs(inputIndex)->FunctionValues(),
                        Inputs(inputIndex)->GradientValues(),
                        mStartLbl, 1);
                }
            }
            else
                return;
        }

        static void ErrorSignalToPostitionDependentNode(const Matrix<ElemType>& gradientValues, const Matrix<ElemType>& labls, const Matrix<ElemType>& postProb, Matrix<ElemType>& grd)
        {
            Matrix<ElemType>::AddScaledDifference(gradientValues, postProb, labls, grd);
        }

        static void ErrorSignalToTransitionNode(
            const Matrix<ElemType>& labls, const Matrix<ElemType>& alpha, const Matrix<ElemType>& beta,
            const Matrix<ElemType>& pair_scores, Matrix<ElemType>& grd,
            const int startLbl, const size_t shift = 1)
        {
            TransGrdCompute(labls,
                alpha,
                beta,
                pair_scores,
                grd,
                startLbl, shift);
        }

        /// compute forward backward algorithm
        /*TODO: merge with call site*/void EvaluateThisNodeS(Matrix<ElemType> postprob, Matrix<ElemType> alpha, Matrix<ElemType> beta, Matrix<ElemType> & functionValues, const Matrix<ElemType> & lbls, const Matrix<ElemType> & pos_scores, const Matrix<ElemType> & pair_scores, int& firstLbl, int& lastLbl, const int iStep = 1)
        {
            /// to-do, each slice is for one sentence
            /// to-do, number of slices correspond to number of frames 
            /// this implementation only supports one sentence per minibatch

            int nObs = lbls.GetNumCols();

            /// change to other values so can support multiple sentences in each minibatch
            assert(iStep == 1);
            ForwardCompute(alpha, lbls, pos_scores, pair_scores);
            BackwardCompute(alpha, beta, functionValues, lbls, pos_scores, pair_scores, iStep);
            PostProbCompute(postprob, alpha, beta);

            firstLbl = -1;
            for (int ik = 0; ik < lbls.GetNumRows(); ik++)
            if (lbls(ik, 0) != 0)
            {
                firstLbl = ik; break;
            }

            lastLbl = -1;
            for (int ik = 0; ik < lbls.GetNumRows(); ik++)
            if (lbls(ik, nObs - 1) != 0)
            {
                lastLbl = ik; break;
            }

            functionValues.AssignInnerProductOfMatrices(lbls, pos_scores);

            Matrix<ElemType> a = alpha.ColumnSlice(nObs - 1, 1);
            ElemType fAlpha;
            fAlpha = a.LogAddSumOfElements();

            /// transition score
            ElemType tscore = 0;
            for (int t = 0; t < nObs - 1; t++){
                int i = -1;
                for (int ik = 0; ik < lbls.GetNumRows(); ik++)
                if (lbls(ik, t) != 0){
                    i = ik; break;
                }
                int j = -1;
                for (int ik = 0; ik < lbls.GetNumRows(); ik++)
                if (lbls(ik, t + 1) != 0){
                    j = ik; break;
                }
                tscore += pair_scores(j, i);
            }
            tscore += functionValues.Get00Element();  /// correct path score
            tscore -= fAlpha;  /// reduced by the scores from all paths
            functionValues.SetValue(tscore);

            functionValues *= (-1);
        }

        /// compute forward backward algorithm
        static void ForwardCompute(Matrix<ElemType>& alpha,
            const Matrix<ElemType>& lbls,
            const Matrix<ElemType>& pos_scores, const Matrix<ElemType>& pair_scores)
        {
            /// to-do, shift more than 1 to support muliple sentences per minibatch
            int iNumPos = lbls.GetNumCols();
            int iNumLab = lbls.GetNumRows();

            int firstLbl = -1;
            for (int ik = 0; ik < lbls.GetNumRows(); ik++)
            if (lbls(ik, 0) != 0){
                firstLbl = ik; break;
            }

            /// need to have 
            alpha.Resize(iNumLab, iNumPos);

            for (int t = 0; t < iNumPos; t++)
            {
                for (int k = 0; k < iNumLab; k++)
                {
                    ElemType fTmp = (ElemType)LZERO;
                    for (int j = 0; j < iNumLab; j++)
                    {
                        ElemType fAlpha = (j == firstLbl) ? (ElemType) 0.0 : (ElemType)LZERO;
                        if (t > 0)
                            fAlpha = alpha(j, t - 1);
                        fTmp = alpha.LogAdd(fTmp, fAlpha + pair_scores(k, j));
                    }
                    fTmp += pos_scores(k, t);  /// include position dependent score
                    alpha(k, t) = fTmp;
                }
            }
        }

        /// compute backward algorithm
        static void BackwardCompute( const Matrix<ElemType>& alpha, Matrix<ElemType>& beta,
            Matrix<ElemType>& functionValues, const Matrix<ElemType>& lbls,
            const Matrix<ElemType>& pos_scores, const Matrix<ElemType>& pair_scores, const int shift = 1)
        {
            assert(shift == 1);

            alpha.RCRFBackwardCompute(alpha, beta, functionValues, lbls, pos_scores, pair_scores, shift);
        }

        static void TransGrdCompute(const Matrix<ElemType>& lbls,
            const Matrix<ElemType>&   alpha,
            const Matrix<ElemType>& beta,
            const Matrix<ElemType>& pair_scores,
            Matrix<ElemType>& grd,
            const int startLbl,
            const int shift = 1)
        {
            assert(shift == 1);

            alpha.RCRFTransGrdCompute(lbls,
                alpha,
                beta,
                pair_scores,
                grd,
                startLbl, shift);
        }

        /// compute forward backward algorithm
        static void PostProbCompute(Matrix<ElemType>& postprob, const Matrix<ElemType>& alpha, const Matrix<ElemType>& beta)
        {
            int iNumPos = alpha.GetNumCols();
            int iNumLab = alpha.GetNumRows();

            postprob.Resize(iNumLab, iNumPos);
            postprob.SetValue(beta);
            postprob.InplaceExp();
        }

        virtual void /*ComputationNodeBase::*/Validate(bool isFinalValidationPass) override
        {
            Base::Validate(isFinalValidationPass);

            if (isFinalValidationPass)
                if (!(Inputs(1)->GetNumRows() == Inputs(2)->GetNumRows() &&  // position dependent and pair scores have same number of labels
                    Inputs(0)->GetNumRows() == Inputs(1)->GetNumRows() &&
                    Inputs(0)->GetNumCols() == Inputs(1)->GetNumCols() && // position dependent and pair scores have the same observation numbers
                    Inputs(2)->GetNumCols() == Inputs(2)->GetNumRows()))
            {
                LogicError("The Matrix dimension in the CRFNode operation does not match.");
            }

            Resize(1,1);
            m_pMBLayout = nullptr;    // this node does not hold mini-batch data
            InferImageDimsFromInputs();
        }

        virtual void InferImageDimsFromInputs()
        {
            InferImageDimsFromInput(0, false);

            m_outputImageLayout = ImageLayout();
        }

        virtual void CopyTo(const ComputationNodePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const
        {
            Base::CopyTo(nodeP, newName, flags);
            if (flags & CopyNodeFlags::copyNodeValue)
            {
                auto node = dynamic_pointer_cast<CRFNode<ElemType>>(nodeP);
                node->mAlpha = mAlpha;
                node->mBeta= mBeta;
                node->mPostProb = mPostProb;

                node->mStartLbl = mStartLbl;
                node->mEndLbl = mEndLbl;
            }
        }
    protected:
        virtual bool NodeDoesItsOwnCustomizedMissingColumnsMasking() { return true; }
    private:
        Matrix<ElemType> mAlpha;    // TODO: m_Alpha etc.
        Matrix<ElemType> mBeta;
        Matrix<ElemType> mPostProb;
        int mStartLbl;
        int mEndLbl;
    };

    // -----------------------------------------------------------------------
    /// DummyCriterionNode (objectives, derivatives, prediction)
    // -----------------------------------------------------------------------

    // This training criterion node needs derivatives and objectives to be
    // computed out of the node. Derivatives and objectives will be fed to the
    // node as input features. It has 3 inputs:
    // 1. feature node that feeds objectives
    // 2. feature node that feeds derivatives
    // 3. neural network output
    //
    // This node is useful in sequence training for speech recognition, so that
    // we can separate lattice computation (which may rely other softwares, such
    // as Kaldi) with the neural network training.
    template<class ElemType>
    class DummyCriterionNode : public ComputationNodeNonLooping/*ComputationNode*/<ElemType>, public NumInputs<3>
    {
        typedef ComputationNodeNonLooping<ElemType> Base; UsingComputationNodeMembersBoilerplate;
        static const std::wstring TypeName() { return L"DummyCriterion"; }
    public:
        DummyCriterionNode(DEVICEID_TYPE deviceId, const wstring & name) :
          Base(deviceId, name)
        { }

        virtual void ComputeInputPartial(const size_t inputIndex)
        {
            if (inputIndex > 2)
                InvalidArgument("DummyCriterionNode only takes three inputs.");
            else if (inputIndex == 0)
                LogicError("DummyCriterionNode: derivatives with respect to objective features are not necessary, not implemented yet.\n");
            else if (inputIndex == 1)
                LogicError("DummyCriterionNode: derivatives with respect to derivative features are not necessary, not implemented yet.\n");
            else
                ComputeInputPartialThree(Inputs(1)->FunctionValues(), Inputs(inputIndex)->GradientValues(), GradientValues());
        }

        /*TODO: merge with call site*/void ComputeInputPartialThree(const Matrix<ElemType>& inputFunctionValues1,
                                                    Matrix<ElemType>& inputGradientValues, const Matrix<ElemType>& gradientValues)  
        {
            Matrix<ElemType>::ScaleAndAdd(gradientValues.Get00Element(), inputFunctionValues1, inputGradientValues);
        }

        virtual void /*ComputationNodeNonLooping::*/EvaluateThisNodeNonLooping() override
        {
            EvaluateThisNodeS(FunctionValues(), Inputs(0)->FunctionValues());
        }

        /*TODO: merge with call site*/void EvaluateThisNodeS(Matrix<ElemType>& functionValues, const Matrix<ElemType>& inputFunctionValues0)  
        {
            if (inputFunctionValues0.GetNumRows() != 1 || inputFunctionValues0.GetNumCols() != 1)
                LogicError("DummyCriterionNode expects first input has dimension (1, 1).\n");
            functionValues.Resize(1, 1);
            functionValues.SetValue(inputFunctionValues0.Get00Element());
#if NANCHECK
            functionValues.HasNan("DummyCriterionNode");
#endif
        }

        virtual void /*ComputationNodeBase::*/Validate(bool isFinalValidationPass) override
        {
            Base::Validate(isFinalValidationPass);

            if (Inputs(0)->OperationName() != L"InputValue")
                LogicError("DummyCriterionNode criterion requires the first input to be computed objectives.");
            if (Inputs(0)->OperationName() != L"InputValue")
                LogicError("DummyCriterionNode criterion requires the first input to be computed derivatives.");
            if (isFinalValidationPass)
            {
                if (Inputs(0)->GetNumRows() != 1)
                LogicError("DummyCriterionNode criterion requires the first input to have dimension 1.");
                if (Inputs(0)->GetNumRows() == 0 || Inputs(1)->GetNumRows() == 0 || Inputs(2)->GetNumRows() == 0)
                    LogicError("DummyCriterionNode operation: one of the operands has 0 elements.");
                if (Inputs(1)->GetNumRows() != Inputs(2)->GetNumRows())
                LogicError("The Matrix dimension in the DummyCriterionNode operation does not match.");
            }
            // TODO: What is this about?
            //if (Inputs(1)->GetNumCols() != Inputs(2)->GetNumCols())
            //    ValidateInferChildDims(1, Inputs(1)->GetNumRows(), Inputs(2)->GetNumCols()); 

            Resize(1,1);
            m_pMBLayout = nullptr;    // this node does not hold mini-batch data
            InferImageDimsFromInputs(); 
        }

        virtual void InferImageDimsFromInputs()
        {
            InferImageDimsFromInput(0, false);

            m_outputImageLayout = ImageLayout();
        }
    protected:
        virtual bool NodeDoesItsOwnCustomizedMissingColumnsMasking() { return true; }
    };

    template class DummyCriterionNode<float>; 
    template class DummyCriterionNode<double>;

    // -----------------------------------------------------------------------
    /// SequenceWithSoftmaxNode (label, prediction, loglikelihood)
    // -----------------------------------------------------------------------

    // discriminative sequence training criterion
    template<class ElemType>
    class SequenceWithSoftmaxNode : public ComputationNodeNonLooping<ElemType>, public NumInputs<3>
    {
        typedef ComputationNodeNonLooping<ElemType> Base; UsingComputationNodeMembersBoilerplate;
        static const std::wstring TypeName() { return L"SequenceWithSoftmax"; }
    public:
        SequenceWithSoftmaxNode(DEVICEID_TYPE deviceId, const wstring & name) :
            Base(deviceId, name),
            m_logSoftmaxOfRight(deviceId), m_softmaxOfRight(deviceId), m_gammaFromLattice(deviceId), m_maskOfFramedrop(deviceId), m_gammaCalcInitialized(false)
        {
        }
        
        //compute gradients to input observations, the weights to the observations, and the class log posterior probabilites
        virtual void ComputeInputPartial(const size_t inputIndex)
        {
            //auto t_start_time = Timer::MilliSecondElapsed();
            //left Node must be a scalar
            if (inputIndex == 0)  //left derivative
            {
                ComputeInputPartialLeft(m_logSoftmaxOfRight, Inputs(inputIndex)->GradientValues(), GradientValues());
            }
            else if (inputIndex == 1)
            {
                ComputeInputPartialRight(m_softmaxOfRight, Inputs(0)->FunctionValues(), Inputs(inputIndex)->GradientValues(), GradientValues(), m_gammaFromLattice,
                    m_hsmoothingWeight, m_frameDropThresh);
                Inputs(inputIndex)->MaskMissingGradientColumnsToZero();
            }
            else if (inputIndex == 2)
            {
#if 1           // no gradient flows to log LLs (but otherwise we leave it to user if, e.g., another node propagates a gradient into there)
                ;   // gradient does not flow here
#else
                Inputs(inputIndex)->SetParameterUpdateRequired(false);
                Inputs(inputIndex)->GradientValues().SetValue(0.0);
#endif
            }
            else
                RuntimeError("SequenceWithSoftmaxNode criterion only takes with respect to label, DNN output and log likelihood.");
        }

        static void WINAPI ComputeInputPartialLeft(const Matrix<ElemType>& logSoftmaxOfRight, Matrix<ElemType>& inputGradientValues,
            const Matrix<ElemType>& gradientValues)
        {
#if DUMPOUTPUT
            logSoftmaxOfRight.Print("SequenceWithSoftmaxNode Partial-logSoftmaxOfRight");
            gradientValues.Print("SequenceWithSoftmaxNode Partial-gradientValues");
            inputGradientValues.Print("SequenceWithSoftmaxNode Partial-Left-in");
#endif

            Matrix<ElemType>::ScaleAndAdd(-gradientValues.Get00Element(), logSoftmaxOfRight, inputGradientValues);
#if DUMPOUTPUT
            inputGradientValues.Print("SequenceWithSoftmaxNode Partial-Left-out");
#endif
        }

        static void WINAPI ComputeInputPartialRight(const Matrix<ElemType>& softmaxOfRight, const Matrix<ElemType>& inputFunctionValues,
            Matrix<ElemType>& inputGradientValues, const Matrix<ElemType>& gradientValues, const Matrix<ElemType> & gammaFromLattice,
            double hsmoothingWeight, double frameDropThresh)
        {
#if DUMPOUTPUT
            softmaxOfRight.Print("SequenceWithSoftmaxNode Partial-softmaxOfRight");
            inputFunctionValues.Print("SequenceWithSoftmaxNode Partial-inputFunctionValues");
            gradientValues.Print("SequenceWithSoftmaxNode Partial-gradientValues");
            inputGradientValues.Print("SequenceWithSoftmaxNode Partial-Right-in");
#endif  
            
            inputGradientValues.AssignSequenceError((ElemType)hsmoothingWeight, inputFunctionValues, softmaxOfRight, gammaFromLattice, gradientValues.Get00Element());            
            inputGradientValues.DropFrame(inputFunctionValues, gammaFromLattice, (ElemType)frameDropThresh);
#if DUMPOUTPUT
            inputGradientValues.Print("SequenceWithSoftmaxNode Partial-Right");
#endif
        }

        // -sum(left_i * log(softmax_i(right)))
        virtual void EvaluateThisNodeNonLooping()
        {
            // Initialize m_GammaCal
            if (!m_gammaCalcInitialized)
            {
                if (m_hmm.hmms.size() == 0)
                {
                    LogicError("SequenceWithSoftmaxNode criterion evaluation requires HMM states to be set.");
                }
                m_GammaCal.init(m_hmm, m_deviceId);
                m_gammaCalcInitialized = true;
            }
            EvaluateThisNodeS(FunctionValues(), Inputs(0)->FunctionValues(), Inputs(1)->FunctionValues(), Inputs(2)->FunctionValues(), m_softmaxOfRight, m_logSoftmaxOfRight, m_gammaFromLattice, m_lattice, m_GammaCal,
                m_uids, m_boundaries,  Inputs(0)->GetMBLayout(), m_extrauttmap, m_doreferencealign);
        }

        /*TODO: merge with call site*/void EvaluateThisNodeS(Matrix<ElemType>& functionValues, Matrix<ElemType>& inputFunctionValues0, Matrix<ElemType>& inputFunctionValues1,
            const Matrix<ElemType>& inputFunctionValues2, Matrix<ElemType>& softmaxOfRight, Matrix<ElemType>& logSoftmaxOfRight, Matrix<ElemType>& gammafromlattice,
            std::vector<shared_ptr<const msra::dbn::latticesource::latticepair>> &lattices, msra::lattices::GammaCalculation<ElemType> &GammaCal, std::vector<size_t> & uids,
            std::vector<size_t> & boundaries,  MBLayoutPtr pMBLayout, std::vector<size_t> &extrauttmap, bool doReferenceAlign)
        {
            //softmax 
            logSoftmaxOfRight.AssignLogSoftmaxOf(inputFunctionValues1, true);
            softmaxOfRight.SetValue(logSoftmaxOfRight);
            softmaxOfRight.InplaceExp();

            size_t sequenceNum = Inputs(1)->GetNumParallelSequences();
            gammafromlattice.SwitchToMatrixType(softmaxOfRight.GetMatrixType(), softmaxOfRight.GetFormat(), false);
            gammafromlattice.Resize(softmaxOfRight.GetNumRows(), softmaxOfRight.GetNumCols());
            GammaCal.calgammaformb(functionValues, lattices, inputFunctionValues2, inputFunctionValues0, gammafromlattice, uids, boundaries, sequenceNum, pMBLayout, extrauttmap, doReferenceAlign);
            
#if NANCHECK
            functionValues.HasNan("SequenceWithSoftmaxNode");
#endif
#if DUMPOUTPUT
            functionValues.Print("SequenceWithSoftmaxNode");
#endif
        }

        virtual void /*ComputationNodeBase::*/Validate(bool isFinalValidationPass) override
        {
            Base::Validate(isFinalValidationPass);

            if (Inputs(0)->OperationName() != L"InputValue" && Inputs(0)->OperationName() != L"SparseInputValue")
                LogicError("SequenceWithSoftmaxNode criterion requires the first input to be the label.");

            if (isFinalValidationPass)
                if (!(Inputs(0)->GetNumRows() == Inputs(1)->GetNumRows() &&  //match size
                    Inputs(1)->GetNumRows() == Inputs(2)->GetNumRows() &&
                    Inputs(0)->GetNumCols() == Inputs(1)->GetNumCols() &&
                    Inputs(1)->GetNumCols() == Inputs(2)->GetNumCols()))
            {
                    LogicError("The Matrix dimension in the SequenceWithSoftmaxNode operation does not match.");
            }

            Resize(1, 1);
            m_pMBLayout = nullptr;  // no layout
            InferImageDimsFromInputs();

            m_logSoftmaxOfRight.Resize(Inputs(0)->GetNumRows(), Inputs(0)->GetNumCols());
            m_softmaxOfRight.Resize(Inputs(0)->GetNumRows(), Inputs(0)->GetNumCols());
            m_gammaFromLattice.Resize(Inputs(0)->GetNumRows(), Inputs(0)->GetNumCols());
            m_maskOfFramedrop.Resize(Inputs(0)->GetNumRows(), Inputs(0)->GetNumCols());
            m_gammatime = 0;
            m_partialtime = 0;
        }

        virtual void InferImageDimsFromInputs()
        {
            InferImageDimsFromInput(0, false);

            m_outputImageLayout = ImageLayout();
        }

        virtual void MoveMatricesToDevice(const DEVICEID_TYPE deviceId)
        {
            Base::MoveMatricesToDevice(deviceId);            
            m_logSoftmaxOfRight.TransferToDeviceIfNotThereAndNotAutoPlace( deviceId, true);
            m_softmaxOfRight.TransferToDeviceIfNotThereAndNotAutoPlace( deviceId, true);
            m_gammaFromLattice.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);
            m_maskOfFramedrop.TransferToDeviceIfNotThereAndNotAutoPlace(deviceId, true);            
        }

        virtual void CopyTo(const ComputationNodePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const
        {
            Base::CopyTo(nodeP, newName, flags);
            
            if (flags & CopyNodeFlags::copyNodeValue)
            {
                auto node = dynamic_pointer_cast<SequenceWithSoftmaxNode<ElemType>>(nodeP);

                node->m_logSoftmaxOfRight = m_logSoftmaxOfRight;
                node->m_softmaxOfRight = m_softmaxOfRight;
                node->m_gammaFromLattice = m_gammaFromLattice;
                node->m_maskOfFramedrop = m_maskOfFramedrop;
                node->m_hsmoothingWeight = m_hsmoothingWeight;
                node->m_frameDropThresh = m_frameDropThresh;
                node->m_doreferencealign = m_doreferencealign;
            }
        }

        // TODO: method names should be CamelCase
        std::vector<shared_ptr<const msra::dbn::latticesource::latticepair>> * getLatticePtr()
        {
            return &m_lattice;
        }

        std::vector<size_t> * getuidprt()
        {
            return &m_uids;
        }

        std::vector<size_t> * getboundaryprt()
        {
            return &m_boundaries;
        }
        std::vector<size_t> * getextrauttmap()
        {
            return &m_extrauttmap;
        }
        msra::asr::simplesenonehmm *gethmm()
        {
            return &m_hmm;
        }

        void SetSmoothWeight(double hsmoothingWeight)
        {
            m_hsmoothingWeight = hsmoothingWeight;
        }
        void SetFrameDropThresh(double frameDropThresh)
        {
            m_frameDropThresh = frameDropThresh;
        }

        void SetReferenceAlign(const bool doreferencealign)
        {
            m_doreferencealign = doreferencealign;
        }

        void gettime(unsigned long long &gammatime, unsigned long long &partialtime)
        {
            gammatime = m_gammatime;
            partialtime = m_partialtime;
        }
    protected:
        virtual bool NodeDoesItsOwnCustomizedMissingColumnsMasking() { return true; }
        Matrix<ElemType> m_logSoftmaxOfRight;
        Matrix<ElemType> m_softmaxOfRight;
        Matrix<ElemType> m_gammaFromLattice;
        Matrix<ElemType> m_maskOfFramedrop;
        double m_frameDropThresh;
        double m_hsmoothingWeight;
        bool m_doreferencealign;
        std::vector<shared_ptr<const msra::dbn::latticesource::latticepair>> m_lattice;
        msra::asr::simplesenonehmm m_hmm;
        msra::lattices::GammaCalculation<ElemType> m_GammaCal;
        bool m_gammaCalcInitialized;
        std::vector<size_t> m_uids;
        std::vector<size_t> m_boundaries;
        std::vector<size_t> m_extrauttmap;

        unsigned long long m_gammatime;
        unsigned long long m_partialtime;
    };

    template class SequenceWithSoftmaxNode<float>;
    template class SequenceWithSoftmaxNode<double>;
}}}