/*
 * Copyright (c) 2022, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

/*
 * @test
 * @bug 8296287
 * @summary Test direct supertypes of java.lang.Object
 * @library /tools/javac/lib
 * @build   JavacTestingAbstractProcessor TestDirectSupertypeObject
 * @compile -processor TestDirectSupertypeObject -proc:only TestDirectSupertypeObject.java
 */

import java.util.*;
import javax.annotation.processing.*;
import javax.lang.model.element.TypeElement;
import javax.lang.model.type.TypeMirror;
import javax.lang.model.util.Types;
import static java.util.Objects.*;

/**
 * Verify java.lang.Object has an empty list of direct supertypes.
 */
public class TestDirectSupertypeObject extends JavacTestingAbstractProcessor {
    public boolean process(Set<? extends TypeElement> annotations,
                           RoundEnvironment roundEnv) {
        if (!roundEnv.processingOver()) {
            TypeMirror objectType = requireNonNull(eltUtils.getTypeElement("java.lang.Object")).asType();
            var objectSupertypes = typeUtils.directSupertypes(objectType);
            if (!objectSupertypes.isEmpty()) {
                messager.printError("Direct supertypes: " + objectSupertypes);
            }
        }
        return true;
    }
}