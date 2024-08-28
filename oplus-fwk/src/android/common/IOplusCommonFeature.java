/*
 * Copyright (C) 2024 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package android.common;

import android.common.OplusFeatureList;

/* loaded from: classes.dex */
public interface IOplusCommonFeature {
    default OplusFeatureList.OplusIndex index() {
        return OplusFeatureList.OplusIndex.End;
    }

    default IOplusCommonFeature getDefault() {
        return null;
    }
}
