/*=auto=========================================================================

Portions (c) Copyright 2005 Brigham and Women\"s Hospital (BWH) All Rights Reserved.

See COPYRIGHT.txt
or http://www.slicer.org/copyright/copyright.txt for details.

Program:   3D Slicer
Module:    $RCSfile: vtkMRMLVolumeDisplayNode.cxx,v $
Date:      $Date: 2006/03/17 15:10:10 $
Version:   $Revision: 1.2 $

=========================================================================auto=*/

// MRML includes
#include "vtkEventBroker.h"
#include "vtkMRMLScalarVolumeDisplayNode.h"
#include "vtkMRMLScene.h"
#include "vtkMRMLProceduralColorNode.h"
#include "vtkMRMLVolumeNode.h"

// VTK includes
#include <vtkAlgorithmOutput.h>
#include <vtkCallbackCommand.h>
#include <vtkColorTransferFunction.h>
#include <vtkImageAccumulate.h>
#include <vtkImageAppendComponents.h>
#include <vtkImageExtractComponents.h>
#include <vtkImageBimodalAnalysis.h>
#include <vtkImageCast.h>
#include <vtkImageData.h>
#include <vtkImageLogic.h>
#include <vtkImageMapToWindowLevelColors.h>
#include <vtkImageStencil.h>
#include <vtkImageThreshold.h>
#include <vtkObjectFactory.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkVersion.h>


// STD includes
#include <cassert>

//----------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLScalarVolumeDisplayNode);

//----------------------------------------------------------------------------
vtkMRMLScalarVolumeDisplayNode::vtkMRMLScalarVolumeDisplayNode()
{
  // Strings
  this->Interpolate = 1;
  this->WindowLevelLocked = false;
  this->AutoWindowLevel = 1;
  this->AutoThreshold = 0;
  this->ApplyThreshold = 0;
  //this->LowerThreshold = VTK_SHORT_MIN;
  //this->UpperThreshold = VTK_SHORT_MAX;

  // try setting a default grayscale color map
  //this->SetDefaultColorMap(0);

  // create and set visualization pipeline
  this->AlphaLogic = vtkImageLogic::New();
  this->MapToColors = vtkImageMapToColors::New();
  this->Threshold = vtkImageThreshold::New();
  this->AppendComponents = vtkImageAppendComponents::New();

  this->ExtractRGB = vtkImageExtractComponents::New();
  this->ExtractAlpha = vtkImageExtractComponents::New();
  this->MultiplyAlpha = vtkImageStencil::New();

  this->MapToWindowLevelColors = vtkImageMapToWindowLevelColors::New();
  this->MapToWindowLevelColors->SetOutputFormatToLuminance();
  this->MapToWindowLevelColors->SetWindow(256.);
  this->MapToWindowLevelColors->SetLevel(128.);

  this->MapToColors->SetOutputFormatToRGBA();
  this->MapToColors->SetInputConnection(this->MapToWindowLevelColors->GetOutputPort() );

  this->ExtractRGB->SetInputConnection(this->MapToColors->GetOutputPort());
  this->ExtractRGB->SetComponents(0,1,2);

  this->ExtractAlpha->SetInputConnection(this->MapToColors->GetOutputPort());
  this->ExtractAlpha->SetComponents(3);

  this->Threshold->ReplaceInOn();
  this->Threshold->SetInValue(255);
  this->Threshold->ReplaceOutOn();
  this->Threshold->SetOutValue(255);
  this->Threshold->SetOutputScalarTypeToUnsignedChar();
  this->Threshold->ThresholdBetween(VTK_SHORT_MIN, VTK_SHORT_MAX);

  this->MultiplyAlpha->SetInputConnection(0, this->ExtractAlpha->GetOutputPort() );
  this->MultiplyAlpha->SetBackgroundValue(0);

  this->AlphaLogic->SetOperationToAnd();
  this->AlphaLogic->SetOutputTrueValue(255);

  this->AlphaLogic->SetInputConnection(0, this->Threshold->GetOutputPort() );
  //this->AlphaLogic->SetInputConnection(1, this->Threshold->GetOutputPort() );
  this->AlphaLogic->SetInputConnection(1, this->MultiplyAlpha->GetOutputPort() );

  this->AppendComponents->RemoveAllInputs();
  this->AppendComponents->AddInputConnection(0, this->ExtractRGB->GetOutputPort() );
  this->AppendComponents->AddInputConnection(0, this->AlphaLogic->GetOutputPort() );

  this->Bimodal = NULL;
  this->Accumulate = NULL;
  this->IsInCalculateAutoLevels = false;

  vtkEventBroker::GetInstance()->AddObservation(
    this, vtkCommand::ModifiedEvent, this, this->MRMLCallbackCommand  , 10000.);
}

//----------------------------------------------------------------------------
vtkMRMLScalarVolumeDisplayNode::~vtkMRMLScalarVolumeDisplayNode()
{
  this->SetAndObserveColorNodeID( NULL);

  this->AlphaLogic->Delete();
  this->MapToColors->Delete();
  this->Threshold->Delete();
  this->AppendComponents->Delete();
  this->MapToWindowLevelColors->Delete();
  this->ExtractRGB->Delete();
  this->ExtractAlpha->Delete();
  this->MultiplyAlpha->Delete();

  if (this->Bimodal)
    {
    this->Bimodal->Delete();
    this->Bimodal = NULL;
    }
  if (this->Accumulate)
    {
    this->Accumulate->Delete();
    this->Accumulate = NULL;
    }
}

//----------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::SetDefaultColorMap()
{
  this->SetAndObserveColorNodeID("vtkMRMLColorTableNodeGrey");
}

//----------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::SetInputImageDataConnection(vtkAlgorithmOutput *imageDataConnection)
{
  if (this->GetInputImageDataConnection() == imageDataConnection)
    {
    return;
    }
  vtkAlgorithm* oldInputImageDataAlgorithm = this->GetInputImageDataConnection() ?
    this->GetInputImageDataConnection()->GetProducer() : 0;

  if (oldInputImageDataAlgorithm != NULL)
    {
    vtkEventBroker::GetInstance()->RemoveObservations(
      oldInputImageDataAlgorithm, vtkCommand::ModifiedEvent, this, this->MRMLCallbackCommand );
    }

  this->Superclass::SetInputImageDataConnection(imageDataConnection);

  vtkAlgorithm* inputImageDataAlgorithm = this->GetInputImageDataConnection() ?
    this->GetInputImageDataConnection()->GetProducer() : 0;
  if (inputImageDataAlgorithm != NULL)
    {
    vtkEventBroker::GetInstance()->AddObservation(
      inputImageDataAlgorithm, vtkCommand::ModifiedEvent, this, this->MRMLCallbackCommand );
    }
}

//----------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode
::SetInputToImageDataPipeline(vtkAlgorithmOutput *imageDataConnection)
{
  this->Threshold->SetInputConnection(imageDataConnection);
  this->MapToWindowLevelColors->SetInputConnection(imageDataConnection);
}

//----------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode
::SetBackgroundImageStencilDataConnection(vtkAlgorithmOutput *imageDataConnection)
{
  this->MultiplyAlpha->SetStencilConnection(imageDataConnection);
}
//----------------------------------------------------------------------------
vtkAlgorithmOutput* vtkMRMLScalarVolumeDisplayNode::GetBackgroundImageStencilDataConnection()
{
  // Input ports:
  // 0 = foreground image, 1 = background image, 2 = stencil
  const int stencilInputPort = 2;
  return this->MultiplyAlpha->GetNumberOfInputConnections(0)>stencilInputPort ?
    this->MultiplyAlpha->GetInputConnection(0,stencilInputPort) : 0;
}

//----------------------------------------------------------------------------
vtkAlgorithmOutput* vtkMRMLScalarVolumeDisplayNode::GetInputImageDataConnection()
{
  return this->MapToWindowLevelColors->GetNumberOfInputConnections(0) ?
    this->MapToWindowLevelColors->GetInputConnection(0,0) : 0;
}

//----------------------------------------------------------------------------
vtkAlgorithmOutput* vtkMRMLScalarVolumeDisplayNode::GetOutputImageDataConnection()
{
  return this->AppendComponents->GetOutputPort();
}

//----------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::WriteXML(ostream& of, int nIndent)
{
  Superclass::WriteXML(of, nIndent);

  {
  std::stringstream ss;
  ss << this->GetWindow();
  of << " window=\"" << ss.str() << "\"";
  }
  {
  std::stringstream ss;
  ss << this->GetLevel();
  of << " level=\"" << ss.str() << "\"";
  }
  {
  std::stringstream ss;
  ss << this->GetUpperThreshold();
  of << " upperThreshold=\"" << ss.str() << "\"";
  }
  {
  std::stringstream ss;
  ss << this->GetLowerThreshold();
  of << " lowerThreshold=\"" << ss.str() << "\"";
  }
  {
  std::stringstream ss;
  ss << this->Interpolate;
  of << " interpolate=\"" << ss.str() << "\"";
  }
  of << " windowLevelLocked=\"" << (this->GetWindowLevelLocked() ? "true" : "false") << "\"";
  {
  std::stringstream ss;
  ss << this->AutoWindowLevel;
  of << " autoWindowLevel=\"" << ss.str() << "\"";
  }
  {
  std::stringstream ss;
  ss << this->ApplyThreshold;
  of << " applyThreshold=\"" << ss.str() << "\"";
  }
  {
  std::stringstream ss;
  ss << this->AutoThreshold;
  of << " autoThreshold=\"" << ss.str() << "\"";
  }
  if (this->WindowLevelPresets.size() > 0)
    {
    for (int p = 0; p < this->GetNumberOfWindowLevelPresets(); p++)
      {
      std::stringstream ss;
      ss << this->WindowLevelPresets[p].Window;
      ss << "|";
      ss << this->WindowLevelPresets[p].Level;
      of << " windowLevelPreset" << p << "=\"" << ss.str() << "\"";
      }
    }
}

//----------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::ReadXMLAttributes(const char** atts)
{
  int disabledModify = this->StartModify();

  Superclass::ReadXMLAttributes(atts);

  const char* attName;
  const char* attValue;
  while (*atts != NULL)
    {
    attName = *(atts++);
    attValue = *(atts++);
    if (!strcmp(attName, "window"))
      {
      std::stringstream ss;
      ss << attValue;
      double window;
      ss >> window;
      this->SetWindow(window);
      }
    else if (!strcmp(attName, "level"))
      {
      std::stringstream ss;
      ss << attValue;
      double level;
      ss >> level;
      this->SetLevel(level);
      }
    else if (!strcmp(attName, "upperThreshold"))
      {
      std::stringstream ss;
      ss << attValue;
      double threshold;
      ss >> threshold;
      this->SetUpperThreshold(threshold);
      }
    else if (!strcmp(attName, "lowerThreshold"))
      {
      std::stringstream ss;
      ss << attValue;
      double threshold;
      ss >> threshold;
      this->SetLowerThreshold(threshold);
      }
    else if (!strcmp(attName, "interpolate"))
      {
      std::stringstream ss;
      ss << attValue;
      ss >> this->Interpolate;
      }
    else if (!strcmp(attName, "windowLevelLocked"))
      {
      this->SetWindowLevelLocked(strcmp(attValue, "true") == 0);
      }
    else if (!strcmp(attName, "autoWindowLevel"))
      {
      std::stringstream ss;
      ss << attValue;
      ss >> this->AutoWindowLevel;
      }
    else if (!strcmp(attName, "applyThreshold"))
      {
      std::stringstream ss;
      ss << attValue;
      ss >> this->ApplyThreshold;
      }
    else if (!strcmp(attName, "autoThreshold"))
      {
      std::stringstream ss;
      ss << attValue;
      ss >> this->AutoThreshold;
      }
    else if (!strncmp(attName, "windowLevelPreset", 17))
      {
      this->AddWindowLevelPresetFromString(attValue);
      }
    }

  this->EndModify(disabledModify);
}

//----------------------------------------------------------------------------
// Copy the node\"s attributes to this object.
// Does NOT copy: ID, FilePrefix, Name, VolumeID
void vtkMRMLScalarVolumeDisplayNode::Copy(vtkMRMLNode *anode)
{
  int disabledModify = this->StartModify();

  Superclass::Copy(anode);
  vtkMRMLScalarVolumeDisplayNode *node = (vtkMRMLScalarVolumeDisplayNode *) anode;

  this->SetWindowLevelLocked(node->GetWindowLevelLocked());
  this->SetAutoWindowLevel( node->GetAutoWindowLevel() );
  this->SetWindowLevel(node->GetWindow(), node->GetLevel());
  this->SetAutoThreshold( node->GetAutoThreshold() ); // don't want to run CalculateAutoLevel
  this->SetApplyThreshold(node->GetApplyThreshold());
  this->SetThreshold(node->GetLowerThreshold(), node->GetUpperThreshold());
  this->SetInterpolate(node->Interpolate);
  for (int p = 0; p < node->GetNumberOfWindowLevelPresets(); p++)
    {
    this->AddWindowLevelPreset(node->GetWindowPreset(p), node->GetLevelPreset(p));
    }

  this->EndModify(disabledModify);

}

//----------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::PrintSelf(ostream& os, vtkIndent indent)
{

  Superclass::PrintSelf(os,indent);

  os << indent << "WindowLevelLocked:   " << (this->WindowLevelLocked ? "true" : "false") << "\n";
  os << indent << "AutoWindowLevel:   " << this->AutoWindowLevel << "\n";
  os << indent << "Window:            " << this->GetWindow() << "\n";
  os << indent << "Level:             " << this->GetLevel() << "\n";
  os << indent << "Window Level Presets:\n";
  for (int p = 0; p < this->GetNumberOfWindowLevelPresets(); p++)
    {
    os << indent.GetNextIndent() << p << " Window: " << this->GetWindowPreset(p) << " | Level: " << this->GetLevelPreset(p) << "\n";
    }
  os << indent << "AutoThreshold:     " << this->AutoThreshold << "\n";
  os << indent << "ApplyThreshold:    " << this->GetApplyThreshold() << "\n";
  os << indent << "UpperThreshold:    " << this->GetUpperThreshold() << "\n";
  os << indent << "LowerThreshold:    " << this->GetLowerThreshold() << "\n";
  os << indent << "Interpolate:       " << this->Interpolate << "\n";
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::ProcessMRMLEvents ( vtkObject *caller,
                                           unsigned long event,
                                           void *callData )
{
  vtkMRMLColorNode* cnode = vtkMRMLColorNode::SafeDownCast(caller);
  if (cnode && event == vtkCommand::ModifiedEvent)
    {
    this->UpdateLookupTable(cnode);
    }
  if (vtkAlgorithmOutput::SafeDownCast(caller) == this->GetScalarImageDataConnection() &&
      this->GetScalarImageDataConnection() &&
      event == vtkCommand::ModifiedEvent)
    {
    this->CalculateAutoLevels();
    }
  if (caller == this && event == vtkCommand::ModifiedEvent &&
      !this->IsInCalculateAutoLevels)
    {
    int wasModifying = this->GetDisableModifiedEvent();
    this->SetDisableModifiedEvent(1);
    this->CalculateAutoLevels();
    // TODO: Reset the pending event counter.
    this->SetDisableModifiedEvent(wasModifying);
    }
  Superclass::ProcessMRMLEvents(caller, event, callData);
}

//---------------------------------------------------------------------------
double vtkMRMLScalarVolumeDisplayNode::GetWindow()
{
  return this->MapToWindowLevelColors->GetWindow();
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::SetWindow(double window)
{
  if (this->MapToWindowLevelColors->GetWindow() == window)
    {
    return;
    }

  this->MapToWindowLevelColors->SetWindow(window);
  this->Modified();
}

//---------------------------------------------------------------------------
double vtkMRMLScalarVolumeDisplayNode::GetLevel()
{
  return this->MapToWindowLevelColors->GetLevel();
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::SetLevel(double level)
{
  if (this->MapToWindowLevelColors->GetLevel() == level)
    {
    return;
    }

  this->MapToWindowLevelColors->SetLevel(level);
  this->Modified();
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::SetWindowLevel(double window, double level)
{
  if (this->MapToWindowLevelColors->GetWindow() == window &&
      this->MapToWindowLevelColors->GetLevel() == level)
    {
    return;
    }

  this->MapToWindowLevelColors->SetWindow(window);
  this->MapToWindowLevelColors->SetLevel(level);
  this->Modified();
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::SetWindowLevelMinMax(double min, double max)
{
  double window = max - min;
  double level = 0.5 * (min + max);
  this->SetWindowLevel(window, level);
}

//---------------------------------------------------------------------------
double vtkMRMLScalarVolumeDisplayNode::GetWindowLevelMin()
{
  const double window = this->GetWindow();
  const double level = this->GetLevel();
  return level - 0.5 * window;
}

//---------------------------------------------------------------------------
double vtkMRMLScalarVolumeDisplayNode::GetWindowLevelMax()
{
  const double window = this->GetWindow();
  const double level = this->GetLevel();
  return level + 0.5 * window;
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::SetApplyThreshold(int apply)
{
  if (this->ApplyThreshold == apply)
    {
    return;
    }
  this->ApplyThreshold = apply;
  this->Threshold->SetOutValue(apply ? 0 : 255);
  this->Modified();
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::SetLowerThreshold(double threshold)
{
  this->SetThreshold(threshold, this->GetUpperThreshold());
}

//---------------------------------------------------------------------------
double vtkMRMLScalarVolumeDisplayNode::GetLowerThreshold()
{
  return this->Threshold->GetLowerThreshold();
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::SetUpperThreshold(double threshold)
{
  this->SetThreshold(this->GetLowerThreshold(), threshold);
}

//---------------------------------------------------------------------------
double vtkMRMLScalarVolumeDisplayNode::GetUpperThreshold()
{
  return this->Threshold->GetUpperThreshold();
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::SetThreshold(double lowerThreshold, double upperThreshold)
{
  if (this->GetLowerThreshold() == lowerThreshold &&
      this->GetUpperThreshold() == upperThreshold)
    {
    return;
    }
  this->Threshold->ThresholdBetween( lowerThreshold, upperThreshold );
  this->Modified();
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::SetColorNodeInternal(vtkMRMLColorNode* newColorNode)
{
  this->Superclass::SetColorNodeInternal(newColorNode);
  this->UpdateLookupTable(newColorNode);
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::UpdateLookupTable(vtkMRMLColorNode* newColorNode)
{
  vtkScalarsToColors *lookupTable = NULL;
  if (newColorNode)
    {
    lookupTable = newColorNode->GetLookupTable();
    if (lookupTable == NULL)
      {
      if (vtkMRMLProceduralColorNode::SafeDownCast(newColorNode) != NULL)
        {
        vtkDebugMacro("UpdateImageDataPipeline: getting color transfer function");
        lookupTable = vtkMRMLProceduralColorNode::SafeDownCast(newColorNode)->GetColorTransferFunction();
        }
      }
    }
  this->MapToColors->SetLookupTable(lookupTable);
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::AddWindowLevelPresetFromString(const char *preset)
{
  // the string is double|double
  double window = 0.0;
  double level = 0.0;

  if (preset == NULL)
    {
    vtkErrorMacro("AddWindowLevelPresetFromString: null input string!");
    return;
    }
  // parse the string
  std::string presetString = std::string(preset);
  char *presetChars = new char [presetString.size()+1];
  strcpy(presetChars, presetString.c_str());
  char *pos = strtok(presetChars, "|");
  if (pos != NULL)
    {
    window = atof(pos);
    }
  pos = strtok(NULL, "|");
  if (pos != NULL)
    {
    level = atof(pos);
    }
  delete[] presetChars;
  this->AddWindowLevelPreset(window, level);
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::AddWindowLevelPreset(double window, double level)
{
  vtkMRMLScalarVolumeDisplayNode::WindowLevelPreset preset;
  preset.Window = window;
  preset.Level = level;

  this->WindowLevelPresets.push_back(preset);
}

//---------------------------------------------------------------------------
int vtkMRMLScalarVolumeDisplayNode::GetNumberOfWindowLevelPresets()
{
  return static_cast<int>(this->WindowLevelPresets.size());
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::SetWindowLevelFromPreset(int p)
{
  if (p < 0 || p >= this->GetNumberOfWindowLevelPresets())
    {
    vtkErrorMacro("SetWindowLevelFromPreset: index " << p << " out of range 0 to " << this->GetNumberOfWindowLevelPresets() - 1);
    return;
    }
  // TODO: change this flag to a regular int showing which preset is being used
  this->SetAutoWindowLevel(0);
  this->SetWindowLevel(this->GetWindowPreset(p), this->GetLevelPreset(p));
}

//---------------------------------------------------------------------------
double vtkMRMLScalarVolumeDisplayNode::GetWindowPreset(int p)
{
  if (p < 0 || p >= this->GetNumberOfWindowLevelPresets())
    {
    vtkErrorMacro("GetWindowPreset: index " << p << " out of range 0 to " << this->GetNumberOfWindowLevelPresets() - 1);
    return 0.0;
    }

  return this->WindowLevelPresets[p].Window;
}

//---------------------------------------------------------------------------
double vtkMRMLScalarVolumeDisplayNode::GetLevelPreset(int p)
{
  if (p < 0 || p >= this->GetNumberOfWindowLevelPresets())
    {
    vtkErrorMacro("GetLevelPreset: index " << p << " out of range 0 to " << this->GetNumberOfWindowLevelPresets() - 1);
    return 0.0;
    }

  return this->WindowLevelPresets[p].Level;
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::ResetWindowLevelPresets()
{
  this->WindowLevelPresets.clear();
}

//---------------------------------------------------------------------------
vtkImageData* vtkMRMLScalarVolumeDisplayNode::GetScalarImageData()
{
  vtkAlgorithm* producer = this->GetScalarImageDataConnection() ?
    this->GetScalarImageDataConnection()->GetProducer() : 0;
  return vtkImageData::SafeDownCast(producer ? producer->GetOutputDataObject(0) : 0);
}

//---------------------------------------------------------------------------
vtkAlgorithmOutput* vtkMRMLScalarVolumeDisplayNode::GetScalarImageDataConnection()
{
  return this->GetInputImageDataConnection();
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::GetDisplayScalarRange(double range[2])
{
  range[0] = 0;
  range[1] = 255.;

  vtkImageData *imageData = this->GetScalarImageData();
  if (!imageData || !this->GetInputImageData())
    {
    // it's a problem if the volume node has an image data but the display node
    // doesn't. It's ok if the display node is not yet in the scene: being
    // loaded (vtkMRMLScene::LoadIntoScene)or stored
    // (vtkMRMLSceneViewNode::StoreScene).
    assert( !this->GetVolumeNode() || !this->GetVolumeNode()->GetImageData() ||
            !this->GetScene() || this->GetScene()->GetNodeByID(this->GetID()) != this);
    vtkDebugMacro( << "No valid image data, returning default values [0, 255]");
    return;
    }
  this->GetScalarImageDataConnection()->GetProducer()->Update();
  imageData->GetScalarRange(range);
  if (imageData->GetNumberOfScalarComponents() >=3 &&
      fabs(range[0]) < 0.000001 && fabs(range[1]) < 0.000001)
    {
    range[0] = 0;
    range[1] = 255;
    }
}

//---------------------------------------------------------------------------
void vtkMRMLScalarVolumeDisplayNode::CalculateAutoLevels()
{
  if (!this->GetAutoWindowLevel() && !this->GetAutoThreshold())
    {
    vtkDebugMacro("CalculateScalarAutoLevels: " << (this->GetID() == NULL ? "nullid" : this->GetID())
                  << ": Auto window level not turned on, returning.");
    return;
    }

  vtkImageData *imageDataScalar = this->GetScalarImageData();
  if (!imageDataScalar)
    {
    vtkDebugMacro("CalculateScalarAutoLevels: input image data is null");
    return;
    }
  // Make sure the point data is up to date.
  // Remember, the display node pipeline is not connected to a consumer (volume
  // display nodes are cloned by the slice logic) therefore no-one has run the
  // filters.
  if (this->GetInputImageData())
    {
    this->GetScalarImageDataConnection()->GetProducer()->Update();
    }

  if (!(imageDataScalar->GetPointData()) ||
      !(imageDataScalar->GetPointData()->GetScalars()))
    {
    vtkDebugMacro("CalculateScalarAutoLevels: input image data is null");
    return;
    }

  if (this->Bimodal == NULL)
    {
    this->Bimodal = vtkImageBimodalAnalysis::New();
    }
  if (this->Accumulate == NULL)
    {
    this->Accumulate = vtkImageAccumulate::New();
    }

  double window = 0.0;
  double level = 0.0;
  double lower = 0.0;
  double upper = 0.0;

  int needAdHoc = 0;
  int scalarType = imageDataScalar->GetScalarType();

  if (imageDataScalar->GetNumberOfScalarComponents() >=3)
    {
    needAdHoc = 1;
    }
  else if (scalarType == VTK_INT ||
           scalarType == VTK_SHORT ||
           scalarType == VTK_CHAR ||
           scalarType == VTK_SIGNED_CHAR ||
           scalarType == VTK_UNSIGNED_CHAR ||
           scalarType == VTK_UNSIGNED_SHORT ||
           scalarType == VTK_UNSIGNED_INT)
    {
    // Data type is VTK_INT or similar, so calculate window/level
    // check the scalar type, bimodal analysis only works on int

    // Setup filter to work with signed 16-bit integer.
    int extent[6] = {0, 65535, 0, 0, 0, 0};
    this->Accumulate->SetComponentExtent(extent);
    double origin[3] = {-32768, 0, 0};
    this->Accumulate->SetComponentOrigin(origin);
    this->Accumulate->SetInputData(imageDataScalar);
    this->Bimodal->SetInputConnection(this->Accumulate->GetOutputPort());
    this->Bimodal->Update();
    // Workaround for image data where all accumulate samples fall
    // within the same histogram bin
    if ( this->Bimodal->GetWindow() == 0.0 &&
         this->Bimodal->GetLevel() == 0.0 )
      {
      needAdHoc = 1;
      }
    else
      {
      window = this->Bimodal->GetWindow();
      level = this->Bimodal->GetLevel();
      lower = this->Bimodal->GetThreshold();
      upper = this->Bimodal->GetMax();
      }
    }
  else if (scalarType == VTK_FLOAT ||
           scalarType == VTK_DOUBLE ||
           scalarType == VTK_LONG ||
           scalarType == VTK_UNSIGNED_LONG)
    {
    // If scalar range is expected to be less conventional, then scale the bins
    // the image accumulate algorithm
    double range[2];
    this->GetDisplayScalarRange(range);
    long minInt = trunc(range[0]) - 1;
    long maxInt = trunc(range[1]) + 1;

    this->Accumulate->SetInputData(imageDataScalar);
    int extent[6] = {0, 999, 0, 0, 0, 0};
    this->Accumulate->SetComponentExtent(extent);
    double origin[3] = {static_cast<double>(minInt), 0.0, 0.0};
    this->Accumulate->SetComponentOrigin(origin);
    double spacing[3] = {(maxInt-minInt)/1000.0, 1.0, 1.0};
    this->Accumulate->SetComponentSpacing(spacing);

    this->Bimodal->SetInputConnection(this->Accumulate->GetOutputPort());
    this->Bimodal->Update();

    // The bimodal analysis assumes that the bin indices correspond directly to
    // voxel intensity values, need to convert back to intensity space
    window = this->Bimodal->GetWindow() * spacing[0];
    level = minInt + (this->Bimodal->GetLevel() - minInt) * spacing[0];
    lower = minInt + (this->Bimodal->GetThreshold() - minInt) * spacing[0];
    upper = minInt + (this->Bimodal->GetMax() - minInt) * spacing[0];
    }
  else
    {
    // If unhandled type, estimate with ad hoc method
    needAdHoc = 1;
    }

  if (needAdHoc)
    {
    vtkDebugMacro("CalculateScalarAutoLevels: image data scalar type is not integer,"
                  " doing ad hoc calc of window/level.");
    double range[2];
    this->GetDisplayScalarRange(range);

    double min = range[0];
    double max = range[1];

    window = max-min;
    level = 0.5*(max+min);
    lower = level;
    upper = range[1];
    }

  this->IsInCalculateAutoLevels = true;
  int disabledModify = this->StartModify();
  if (this->GetAutoWindowLevel())
    {
    this->SetWindowLevel(window, level);
    }
  if (this->GetAutoThreshold())
    {
    this->SetThreshold(lower, upper);
    }
  vtkDebugMacro("CalculateScalarAutoLevels:"
                << " window: " << window << " level: " << level
                << " lower: " << lower << " upper: " << upper);
  this->EndModify(disabledModify);
  this->IsInCalculateAutoLevels = false;
}
