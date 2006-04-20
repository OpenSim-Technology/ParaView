/*=========================================================================

  Program:   ParaView
  Module:    vtkPVAreaSelect.h

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
// .NAME vtkPVAreaSelect - Client side UI for a selection rectangle.
// .SECTION Description
// Creates a client GUI page that makes it possible to draw a rectangle on the
// render window and select all points and cells of a dataset that lie within.

#ifndef __vtkPVAreaSelect_h
#define __vtkPVAreaSelect_h

#include "vtkPVSource.h"

class vtkKWPushButton;
class vtkCallbackCommand;
class vtkRenderer;
class vtkAbstractPicker;
class vtkAreaPicker;
class vtkInteractorObserver;
class vtkInteractorStyleRubberBandPick;

class VTK_EXPORT vtkPVAreaSelect : public vtkPVSource
{
public:
  static vtkPVAreaSelect* New();
  vtkTypeRevisionMacro(vtkPVAreaSelect, vtkPVSource);

  // Description:
  // Set up the UI.
  virtual void CreateProperties();

  // Description:
  // Respond to the button.
  void SelectCallback();

  // Description:
  // Mouse event callbacks in the render window to define selection area with.
  void OnLeftButtonDown(int x, int y);
  void OnLeftButtonUp(int x, int y, vtkRenderer *renderer);

  //These two are for internal use and playback only.
  void CreateVert(int i, double v0, double v1, double v2, double v3);
  void SetVerts(int wireframe);

protected:
  vtkPVAreaSelect();
  ~vtkPVAreaSelect();

  virtual void AcceptCallbackInternal();
  void DoSelect();  

  //to watch mouse with
  vtkCallbackCommand* EventCallbackCommand;
  static void ProcessEvents(vtkObject* object, 
                            unsigned long event,
                            void* clientdata, 
                            void* calldata);
  

  //to use while picking
  vtkInteractorStyleRubberBandPick *RubberBand;

  //to restore after picking
  vtkInteractorObserver *SavedStyle;

  //to turn on selection
  vtkKWPushButton *SelectButton;

  //have to help out trace and state saving because UI is not completely
  //XML controlled
  void SaveVertsInTrace();

  int SelectReady;
  int InPickState;
  int Xs;
  int Ys;
  int Xe;
  int Ye;
  double Verts[32];

private:
  vtkPVAreaSelect(const vtkPVAreaSelect&); // Not implemented
  void operator=(const vtkPVAreaSelect&); // Not implemented
};

#endif
