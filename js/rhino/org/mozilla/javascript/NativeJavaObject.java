/* -*- Mode: java; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1997-1999 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

package org.mozilla.javascript;

import java.lang.reflect.*;
import java.util.Hashtable;
import java.util.Enumeration;

/**
 * This class reflects non-Array Java objects into the JavaScript environment.  It
 * reflect fields directly, and uses NativeJavaMethod objects to reflect (possibly
 * overloaded) methods.<p>
 *
 * @author Mike Shaver
 * @see NativeJavaArray
 * @see NativeJavaPackage
 * @see NativeJavaClass
 */

public class NativeJavaObject implements Scriptable, Wrapper {

    public NativeJavaObject(Scriptable scope, Object javaObject, 
                            JavaMembers members) 
    {
        this.parent = scope;
        this.javaObject = javaObject;
        this.members = members;
    }
    
    public NativeJavaObject(Scriptable scope, Object javaObject, 
                            Class staticType) 
    {
        this.parent = scope;
        this.javaObject = javaObject;
        Class dynamicType = javaObject != null ? javaObject.getClass()
            : staticType;
        members = JavaMembers.lookupClass(scope, dynamicType, staticType);
        fieldAndMethods = members.getFieldAndMethodsObjects(javaObject, false);
    }
    
    public boolean has(String name, Scriptable start) {
        return members.has(name, false);
    }
        
    public boolean has(int index, Scriptable start) {
        return false;
    }
        
    public Object get(String name, Scriptable start) {
        if (fieldAndMethods != null) {
            Object result = fieldAndMethods.get(name);
            if (result != null)
                return result;
        }
        // TODO: passing 'this' as the scope is bogus since it has 
        //  no parent scope
        return members.get(this, name, javaObject, false);
    }

    public Object get(int index, Scriptable start) {
        throw members.reportMemberNotFound(Integer.toString(index));
    }
    
    public void put(String name, Scriptable start, Object value) {
        members.put(name, javaObject, value, false);
    }

    public void put(int index, Scriptable start, Object value) {
        throw members.reportMemberNotFound(Integer.toString(index));
    }

    public boolean hasInstance(Scriptable value) {
        // This is an instance of a Java class, so always return false
        return false;
    }
    
    public void delete(String name) {
    }
    
    public void delete(int index) {
    }
    
    public Scriptable getPrototype() {
        if (javaObject.getClass() == ScriptRuntime.StringClass) {
            return ScriptableObject.getClassPrototype(parent, "String");
        }
        return null;
    }

    public void setPrototype(Scriptable prototype) {
    }

    /**
     * Returns the parent (enclosing) scope of the object.
     */
    public Scriptable getParentScope() {
        return parent;
    }

    /**
     * Sets the parent (enclosing) scope of the object.
     */
    public void setParentScope(Scriptable m) {
        parent = m;
    }

    public Object[] getIds() {
        return members.getIds(false);
    }
    
    public static Object wrap(Scriptable scope, Object obj, Class staticType) 
    {
        if (obj == null)
            return obj;
        Class cls = obj.getClass();
        if (staticType != null && staticType.isPrimitive()) {
            if (staticType == Void.TYPE)
                return Undefined.instance;
            if (staticType == Character.TYPE)
                return new Integer((int) ((Character) obj).charValue());
            return obj;
        }
        if (cls.isArray())
            return NativeJavaArray.wrap(scope, obj);
        if (obj instanceof Scriptable)
            return obj;
        if (Context.useJSObject && jsObjectClass != null && 
            staticType != jsObjectClass && jsObjectClass.isInstance(obj)) 
            {
                try {
                    return jsObjectGetScriptable.invoke(obj, ScriptRuntime.emptyArgs);
                }
                catch (InvocationTargetException e) {
                    // Just abandon conversion from JSObject
                }
                catch (IllegalAccessException e) {
                    // Just abandon conversion from JSObject
                }
            }
        return new NativeJavaObject(scope, obj, staticType);
    }

    public Object unwrap() {
        return javaObject;
    }

    public String getClassName() {
        return "JavaObject";
    }

    Function getConverter(String converterName) {
        Object converterFunction = get(converterName, this);
        if (converterFunction instanceof Function) {
            return (Function) converterFunction;
        }
        return null;
    }

    Object callConverter(Function converterFunction)
        throws JavaScriptException
    {
        Function f = (Function) converterFunction;
        return f.call(Context.getContext(), f.getParentScope(),
                      this, new Object[0]);
    }

    Object callConverter(String converterName)
        throws JavaScriptException
    {
        Function converter = getConverter(converterName);
        if (converter == null) {
            Object[] errArgs = { converterName, javaObject.getClass().getName() };
            throw Context.reportRuntimeError(
                                             Context.getMessage("msg.java.conversion.implicit_method",
                                                                errArgs));
        }
        return callConverter(converter);
    }

    public Object getDefaultValue(Class hint) {
        if (hint == null || hint == ScriptRuntime.StringClass)
            return javaObject.toString();
        try {
            if (hint == ScriptRuntime.BooleanClass)
                return callConverter("booleanValue");
            if (hint == ScriptRuntime.NumberClass) {
                return callConverter("doubleValue");
            }
            // fall through to error message
        } catch (JavaScriptException jse) {
            // fall through to error message
        }
        throw Context.reportRuntimeError(
                                         Context.getMessage("msg.default.value", null));
    }


    /**
     * Determine whether we can/should convert between the given type and the
     * desired one.  This should be superceded by a conversion-cost calculation
     * function, but for now I'll hide behind precedent.
     */
    public static boolean canConvert(Object fromObj, Class to) {
        int weight = NativeJavaObject.getConversionWeight(fromObj, to);

        return (weight < CONVERSION_NONE);
    }

    static final int JSTYPE_UNDEFINED   = 0; // undefined type
    static final int JSTYPE_NULL        = 1; // null
    static final int JSTYPE_BOOLEAN     = 2; // boolean
    static final int JSTYPE_NUMBER      = 3; // number
    static final int JSTYPE_STRING      = 4; // string
    static final int JSTYPE_JAVA_CLASS  = 5; // JavaClass
    static final int JSTYPE_JAVA_OBJECT = 6; // JavaObject
    static final int JSTYPE_JAVA_ARRAY  = 7; // JavaArray
    static final int JSTYPE_OBJECT      = 8; // Scriptable

    public static final byte CONVERSION_TRIVIAL      = 1;
    public static final byte CONVERSION_NONTRIVIAL   = 0;
    public static final byte CONVERSION_NONE         = 99;

    /**
     * Derive a ranking based on how "natural" the conversion is.
     * The special value CONVERSION_NONE means no conversion is possible, 
     * and CONVERSION_NONTRIVIAL signals that more type conformance testing 
     * is required.
     * Based on 
     * <a href="http://www.mozilla.org/js/liveconnect/lc3_method_overloading.html">
     * "preferred method conversions" from Live Connect 3</a>
     */
    public static int getConversionWeight(Object fromObj, Class to) {
        int fromCode = NativeJavaObject.getJSTypeCode(fromObj);

        int result = CONVERSION_NONE;

        switch (fromCode) {

        case JSTYPE_UNDEFINED:
            if (to == ScriptRuntime.StringClass || 
                to == ScriptRuntime.ObjectClass) {
                result = 1;
            }
            break;

        case JSTYPE_NULL:
            if (!to.isPrimitive()) {
                result = 1;
            }
            break;

        case JSTYPE_BOOLEAN:
            // "boolean" is #1
            if (to == Boolean.TYPE) {
                result = 1;
            }
            else if (to == ScriptRuntime.BooleanClass) {
                result = 2;
            }
            else if (to == ScriptRuntime.ObjectClass) {
                result = 3;
            }
            else if (to == ScriptRuntime.StringClass) {
                result = 4;
            }
            break;

        case JSTYPE_NUMBER:
            if (to.isPrimitive()) {
                if (to == Double.TYPE) {
                    result = 1;
                }
                else if (to != Boolean.TYPE) {
                    result = 1 + NativeJavaObject.getSizeRank(to);
                }
            }
            else {
                if (to == ScriptRuntime.StringClass) {
                    // native numbers are #1-8
                    result = 9;
                }
                else if (to == ScriptRuntime.ObjectClass) {
                    result = 10;
                }
                else if (ScriptRuntime.NumberClass.isAssignableFrom(to)) {
                    // "double" is #1
                    result = 2;
                }
            }
            break;

        case JSTYPE_STRING:
            if (to == ScriptRuntime.StringClass) {
                result = 1;
            }
            else if (to == ScriptRuntime.ObjectClass) {
                result = 2;
            }
            else if (to.isPrimitive() && to != Boolean.TYPE) {
                if (to == Character.TYPE) {
                    result = 3;
                }
                else {
                    result = 4;
                }
            }
            break;

        case JSTYPE_JAVA_CLASS:
            if (to == ScriptRuntime.ClassClass) {
                result = 1;
            }
            else if (Context.useJSObject && jsObjectClass != null && 
                jsObjectClass.isAssignableFrom(to)) {
                result = 2;
            }
            else if (to == ScriptRuntime.ObjectClass) {
                result = 3;
            }
            else if (to == ScriptRuntime.StringClass) {
                result = 4;
            }
            break;

        case JSTYPE_JAVA_OBJECT:
        case JSTYPE_JAVA_ARRAY:
            if (to == ScriptRuntime.StringClass) {
                result = 2;
            }
            else if (to.isPrimitive() && to != Boolean.TYPE) {
                result = 
                    (fromCode == JSTYPE_JAVA_ARRAY) ?
                    CONVERSION_NONTRIVIAL :
                    2 + NativeJavaObject.getSizeRank(to);
            }
            else {
                Object javaObj = fromObj;
                if (javaObj instanceof NativeJavaObject) {
                    javaObj = ((NativeJavaObject)javaObj).unwrap();
                }
                if (to.isInstance(javaObj)) {
                    result = CONVERSION_NONTRIVIAL;
                }
            }
            break;

        case JSTYPE_OBJECT:
            // Other objects takes #1-#3 spots
            if (Context.useJSObject && jsObjectClass != null && 
                jsObjectClass.isAssignableFrom(to)) {
                result = 1;
            }
            else if (to == ScriptRuntime.ObjectClass) {
                result = 2;
            }
            else if (to == ScriptRuntime.StringClass) {
                result = 3;
            }
            else if (to.isPrimitive() || to != Boolean.TYPE) {
                result = 3 + NativeJavaObject.getSizeRank(to);
            }
            break;
        }

        return result;
    
    }

    static int getSizeRank(Class aType) {
        if (aType == Double.TYPE) {
            return 1;
        }
        else if (aType == Float.TYPE) {
            return 2;
        }
        else if (aType == Long.TYPE) {
            return 3;
        }
        else if (aType == Integer.TYPE) {
            return 4;
        }
        else if (aType == Short.TYPE) {
            return 5;
        }
        else if (aType == Character.TYPE) {
            return 6;
        }
        else if (aType == Byte.TYPE) {
            return 7;
        }
        else if (aType == Boolean.TYPE) {
            return CONVERSION_NONE;
        }
        else {
            return 8;
        }
    }

    static int getJSTypeCode(Object value) {
        if (value == null) {
            return JSTYPE_NULL;
        }
        else if (value == Undefined.instance) {
            return JSTYPE_UNDEFINED;
        }
        else if (value instanceof Scriptable) {
            if (value instanceof NativeJavaClass) {
                return JSTYPE_JAVA_CLASS;
            }
            else if (value instanceof NativeJavaArray) {
                return JSTYPE_JAVA_ARRAY;
            }
            else if (value instanceof NativeString) {
                return JSTYPE_STRING;
            }
            else if (value instanceof NativeJavaObject) {
                return JSTYPE_JAVA_OBJECT;
            }
            else {
                return JSTYPE_OBJECT;
            }
        }
        else {
            Class valueClass = value.getClass();

            if (valueClass == ScriptRuntime.StringClass) {
                return JSTYPE_STRING;
            }
            else if (valueClass == ScriptRuntime.BooleanClass) {
                return JSTYPE_BOOLEAN;
            }
            else if (value instanceof Number) {
                return JSTYPE_NUMBER;
            }
            else if (valueClass == ScriptRuntime.ClassClass) {
                return JSTYPE_JAVA_CLASS;
            }
            else if (valueClass.isArray()) {
                return JSTYPE_JAVA_ARRAY;
            }
            else {
                return JSTYPE_JAVA_OBJECT;
            }
        }
    }

    /**
     * Type-munging for field setting and method invocation.
     * Conforms to LC3 specification
     */
    public static Object coerceType(Class type, Object value) {
        // Don't coerce null to a string (or other object)
        if (value == null) {
            // raise error if type.isPrimitive()
            if (type.isPrimitive()) {
                reportConversionError(value, type);
            }
            return null;
        }
        else if (value == Undefined.instance) {
            if (type == ScriptRuntime.StringClass || 
                type == ScriptRuntime.ObjectClass) {
                return "undefined";
            }
            else {
                // report conversion error
                reportConversionError("undefined", type);
            }
        }

        // Special case: converting JS numbers to Objects
        // Done before we unwrap, to distinguish from a 
        // wrapped java.lang.Number.
        if (type == ScriptRuntime.ObjectClass && value instanceof Number) {
            return new Double(((Number)value).doubleValue());
        }

        // Unwrap at this point; callers do not need to unwrap
        if (value instanceof Wrapper) {
            value = ((Wrapper)value).unwrap();
        }

        // For final classes we can compare valueClass to a Class object
        // rather than using instanceof
        Class valueClass = value.getClass();
        
        // Is value already of the correct type?
        if (valueClass == type)
            return value;

        // String
        if (type == ScriptRuntime.StringClass)
            return ScriptRuntime.toString(value);
        
        // Boolean
        if (type == Boolean.TYPE || type == ScriptRuntime.BooleanClass) {
            // Under LC3, only JS Booleans can be coerced into a Boolean value
            if (valueClass == ScriptRuntime.BooleanClass) {
                return value;
            }
            else {
                reportConversionError(value, type);
            }
        }

        // Character
        if (type == Character.TYPE || type == ScriptRuntime.CharacterClass) {
            // Special case for converting a single char string to a character
            if (valueClass == ScriptRuntime.StringClass && ((String) value).length() == 1)
                return new Character(((String) value).charAt(0));
            /*
            if (valueClass == ScriptRuntime.StringClass) {
                String string = (String)value;
                if (string.length() == 1) {
                    char ch = string.charAt(0);
                    // XXX: Next test not in LC3 spec, but is backwardly 
                    // compatible and satisfies regression tests
                    if (!Character.isDigit(ch)) {
                        return new Character(ch);
                    }
                }
            }
            */
            if (valueClass == ScriptRuntime.CharacterClass) {
                return value;
            }
            return new Character((char)toInteger(value, 
                                                 ScriptRuntime.CharacterClass,
                                                 Character.MIN_VALUE,
                                                 Character.MAX_VALUE));
        }

        // Double, Float
        if (type == ScriptRuntime.DoubleClass || type == Double.TYPE) {
            return valueClass == ScriptRuntime.DoubleClass
                ? value
                : new Double(ScriptRuntime.toNumber(value));
        }

        if (type == ScriptRuntime.FloatClass || type == Float.TYPE) {
            if (valueClass == ScriptRuntime.FloatClass) {
                return value;
            }
            else {
                double number = ScriptRuntime.toNumber(value);
                if (Double.isInfinite(number) || Double.isNaN(number)
                    || number == 0.0) {
                    return new Float((float)number);
                }
                else {
                    double absNumber = Math.abs(number);
                    if (absNumber < (double)Float.MIN_VALUE) {
                        return new Float((number > 0.0) ? +0.0 : -0.0);
                    }
                    else if (absNumber > (double)Float.MAX_VALUE) {
                        return new Float((number > 0.0) ?
                                         Float.POSITIVE_INFINITY : 
                                         Float.NEGATIVE_INFINITY);
                    }
                    else {
                        return new Float((float)number);
                    }
                }
            }
        }

        // Integer, Long, Short, Byte
        if (type == ScriptRuntime.IntegerClass || type == Integer.TYPE) {
            if (valueClass == ScriptRuntime.IntegerClass) {
                return value;
            }
            else {
                return new Integer((int)toInteger(value, 
                                                  ScriptRuntime.IntegerClass,
                                                  Integer.MIN_VALUE,
                                                  Integer.MAX_VALUE));
            }
        }

        if (type == ScriptRuntime.LongClass || type == Long.TYPE) {
            if (valueClass == ScriptRuntime.LongClass) {
                return value;
            }
            else {
                return new Long(toInteger(value, 
                                          ScriptRuntime.LongClass,
                                          Long.MIN_VALUE,
                                          Long.MAX_VALUE));
            }
        }

        if (type == ScriptRuntime.ShortClass || type == Short.TYPE) {
            if (valueClass == ScriptRuntime.ShortClass) {
                return value;
            }
            else {
                return new Short((short)toInteger(value, 
                                                  ScriptRuntime.ShortClass,
                                                  Short.MIN_VALUE,
                                                  Short.MAX_VALUE));
            }
        }

        if (type == ScriptRuntime.ByteClass || type == Byte.TYPE) {
            if (valueClass == ScriptRuntime.ByteClass) {
                return value;
            }
            else {
                return new Byte((byte)toInteger(value, 
                                                ScriptRuntime.ByteClass,
                                                Byte.MIN_VALUE,
                                                Byte.MAX_VALUE));
            }
        }

        // If JSObject compatibility is enabled, and the method wants it,
        // wrap the Scriptable value in a JSObject.
        if (Context.useJSObject && jsObjectClass != null && 
            value instanceof Scriptable)
            {
                if (ScriptRuntime.ScriptableClass.isAssignableFrom(type))
                    return value;
                try {
                    Object ctorArgs[] = { value };
                    return jsObjectCtor.newInstance(ctorArgs);
                } catch (InstantiationException instEx) {
                    throw new EvaluatorException("error generating JSObject wrapper for " +
                                                 value);
                } catch (IllegalArgumentException argEx) {
                    throw new EvaluatorException("JSObject constructor doesn't want [Scriptable]!");
                } catch (InvocationTargetException e) {
                    throw WrappedException.wrapException(e);
                } catch (IllegalAccessException accessEx) {
                    throw new EvaluatorException("JSObject constructor is protected/private!");
                }
            }
        
        return value;
    }

    static long toInteger(Object value, Class type, long min, long max) {
        double d;

        if (value instanceof Number) {
            d = ((Number)value).doubleValue();
        }
        else if (value instanceof String) {
            d = ScriptRuntime.toNumber((String)value);
        }
        else if (value instanceof Scriptable) {
            d = ScriptRuntime.toNumber(value);
        }
        else {
            // XXX: is this correct?
            d = ScriptRuntime.toNumber(value.toString());
        }

        if (Double.isInfinite(d) || Double.isNaN(d)) {
            // Convert to string first, for more readable message
            reportConversionError(value.toString(), type);
        }

        if (d > 0.0) {
            d = Math.floor(d);
        }
        else {
            d = Math.ceil(d);
        }

        if (d < (double)min || d > (double)max) {
            // Convert to string first, for more readable message
            reportConversionError(value.toString(), type);
        }
        return (long)d;
    }

    static void reportConversionError(Object value, Class type) {
        Object[] args = {value, type};
        throw Context.reportRuntimeError(Context.getMessage("msg.conversion.not.allowed", args));
    }

    public static void initJSObject() {
        if (!Context.useJSObject)
            return;
        // if netscape.javascript.JSObject is in the CLASSPATH, enable JSObject
        // compatability wrappers
        jsObjectClass = null;
        try {
            jsObjectClass = Class.forName("netscape.javascript.JSObject");
            Class ctorParms[] = { ScriptRuntime.ScriptableClass };
            jsObjectCtor = jsObjectClass.getConstructor(ctorParms);
            jsObjectGetScriptable = jsObjectClass.getMethod("getScriptable", 
                                                            new Class[0]);
        } catch (ClassNotFoundException classEx) {
            // jsObjectClass already null
        } catch (NoSuchMethodException methEx) {
            // jsObjectClass already null
        }
    }
        
    /**
     * The parent scope of this object.
     */
    protected Scriptable parent;

    protected Object javaObject;
    protected JavaMembers members;
    private Hashtable fieldAndMethods;
    static Class jsObjectClass;
    static Constructor jsObjectCtor;
    static Method jsObjectGetScriptable;
}

