//  index.ts
//
//  Created by Kalila L. on Sep. 4th, 2021.
//  Copyright 2021 Vircadia contributors.
//  Copyright 2021 DigiSomni LLC.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html

import { store } from "quasar/wrappers";
import { InjectionKey } from "vue";
import {
    createStore,
    Store as VuexStore,
    useStore as vuexUseStore
} from "vuex";

// import example from './module-example'
import { MainState } from "./modules/state";

/*
* If not building with SSR mode, you can
* directly export the Store instantiation;
*
* The function below can be async too; either use
* async/await or return a Promise which resolves
* with the Store instance.
*/

export interface StateInterface {
    // Define your own store structure, using submodules if needed
    // example: ExampleStateInterface;
    // Declared as unknown to avoid linting issue. Best to strongly type as per the line above.
    MainState: MainState
}

// provide typings for `this.$store`
declare module "@vue/runtime-core" {
    interface ComponentCustomProperties {
        $store: VuexStore<StateInterface>
    }
}

// provide typings for `useStore` helper
export const storeKey: InjectionKey<VuexStore<StateInterface>> = Symbol("vuex-key");

export default store(function (/* { ssrContext } */) {
    const Store = createStore<StateInterface>({
        modules: {
            // example
        },

        // enable strict mode (adds overhead!)
        // for dev mode and --debug builds only
        strict: !!process.env.DEBUGGING
    });

    return Store;
});

export function useStore () {
    return vuexUseStore(storeKey);
}
