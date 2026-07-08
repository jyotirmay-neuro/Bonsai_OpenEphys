#pragma once
/*
 * Open Ephys plugin-API compatibility shim.
 *
 * A plugin binary is bound to one plugin API version: PluginManager rejects any
 * library whose getLibInfo().apiVersion != PLUGIN_API_VER, and the DLL imports
 * JUCE symbols from the host executable. So we ship one binary per GUI line and
 * paper over the *source* differences here.
 *
 *   GUI 1.0.x  -> PLUGIN_API_VER 10  (JUCE 8)
 *   GUI 0.6.x  -> PLUGIN_API_VER  8  (JUCE 6)
 *
 * Everything OEconnect needs that is IDENTICAL across both -- addTTLChannel,
 * setTTLState, broadcastMessage, getContinuousChannel(int), getSampleRate(int),
 * CoreServices::RecordNode::setRecordingDirectory -- is deliberately absent from
 * this header. Only genuine divergences appear below.
 *
 * The parameter-adding and parameter-editor calls are *protected* members of
 * GenericProcessor / GenericEditor, so these are macros meant to be expanded
 * inside those subclasses, not free functions.
 */

#include <ProcessorHeaders.h>
#include <PluginInfo.h>

#ifndef PLUGIN_API_VER
#  error "PLUGIN_API_VER not defined -- include the Open Ephys plugin headers first"
#endif

#if PLUGIN_API_VER >= 10
#  define OEC_OE_API_V10 1
#elif PLUGIN_API_VER == 8
#  define OEC_OE_API_V8 1
#else
#  error "Unsupported Open Ephys plugin API. OEconnect targets GUI 0.6.x (v8) and 1.0.x (v10)."
#endif

/* ---- Parameter scope --------------------------------------------------- */
#if defined(OEC_OE_API_V10)
#  define OEC_SCOPE Parameter::PROCESSOR_SCOPE
#else
#  define OEC_SCOPE Parameter::GLOBAL_SCOPE
#endif

/* ---- Parameter registration -------------------------------------------- *
 * v10 registers via a registerParameters() override; v8 has no such hook and
 * expects parameters to be added from the constructor. Both call the same
 * registerOecParameters() body.
 */
#if defined(OEC_OE_API_V10)
#  define OEC_REGISTER_PARAMS_HOOK  void registerParameters() override { registerOecParameters(); }
#  define OEC_REGISTER_PARAMS_IN_CTOR() ((void)0)
#else
#  define OEC_REGISTER_PARAMS_HOOK  /* v8: registered from the constructor */
#  define OEC_REGISTER_PARAMS_IN_CTOR() registerOecParameters()
#endif

/* ---- Adding parameters -------------------------------------------------- *
 * v10 gained a `displayName` argument between `name` and `description`.
 * On v8 the display name is dropped; the description still becomes the tooltip.
 */
#if defined(OEC_OE_API_V10)
#  define OEC_ADD_BOOL(name, disp, desc, def, deact) \
      addBooleanParameter(OEC_SCOPE, name, disp, desc, def, deact)
#  define OEC_ADD_INT(name, disp, desc, def, lo, hi, deact) \
      addIntParameter(OEC_SCOPE, name, disp, desc, def, lo, hi, deact)
#  define OEC_ADD_STRING(name, disp, desc, def, deact) \
      addStringParameter(OEC_SCOPE, name, disp, desc, def, deact)
#  define OEC_ADD_CATEGORICAL(name, disp, desc, cats, defIdx, deact) \
      addCategoricalParameter(OEC_SCOPE, name, disp, desc, cats, defIdx, deact)
/* v10 takes Array<String>; v8 takes StringArray. */
#  define OEC_CATEGORIES(sa) Array<String>((sa).begin(), (sa).size())
#else
#  define OEC_ADD_BOOL(name, disp, desc, def, deact) \
      addBooleanParameter(OEC_SCOPE, name, desc, def, deact)
#  define OEC_ADD_INT(name, disp, desc, def, lo, hi, deact) \
      addIntParameter(OEC_SCOPE, name, desc, def, lo, hi, deact)
#  define OEC_ADD_STRING(name, disp, desc, def, deact) \
      addStringParameter(OEC_SCOPE, name, desc, def, deact)
#  define OEC_ADD_CATEGORICAL(name, disp, desc, cats, defIdx, deact) \
      addCategoricalParameter(OEC_SCOPE, name, desc, cats, defIdx, deact)
#  define OEC_CATEGORIES(sa) (sa)
#endif

/* ---- Parameter editors (inside a GenericEditor subclass) ---------------- *
 * v10 takes a scope argument and calls the boolean control a "toggle";
 * v8 takes no scope and calls it a "check box".
 */
#if defined(OEC_OE_API_V10)
#  define OEC_ADD_COMBO_EDITOR(name, x, y)  addComboBoxParameterEditor(OEC_SCOPE, name, x, y)
#  define OEC_ADD_TOGGLE_EDITOR(name, x, y) addToggleParameterEditor(OEC_SCOPE, name, x, y)
#  define OEC_ADD_TEXT_EDITOR(name, x, y)   addTextBoxParameterEditor(OEC_SCOPE, name, x, y)
#else
#  define OEC_ADD_COMBO_EDITOR(name, x, y)  addComboBoxParameterEditor(name, x, y)
#  define OEC_ADD_TOGGLE_EDITOR(name, x, y) addCheckBoxParameterEditor(name, x, y)
#  define OEC_ADD_TEXT_EDITOR(name, x, y)   addTextBoxParameterEditor(name, x, y)
#endif

/* ---- JUCE differences ---------------------------------------------------- */
#if defined(OEC_OE_API_V10)
#  define OEC_FONT(sizePx)  FontOptions(sizePx)          /* JUCE 8 */
#  define OEC_WARNING_ICON  MessageBoxIconType::WarningIcon
#else
#  define OEC_FONT(sizePx)  Font(sizePx)                 /* JUCE 6 */
#  define OEC_WARNING_ICON  AlertWindow::WarningIcon
#endif
