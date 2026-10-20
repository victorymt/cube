/** Host half of the Voxelcraft Dev bundle. */
import { createVoxelcraftRunTool } from "./lib.js";

/** Cordis plugin name. */
export const name = "voxelcraft-dev";

/** Services required to register and execute the scene runner. */
export const inject = ["tools", "workingDirectory"];

/**
 * Register the Voxelcraft scene runner for model and PTC callers.
 * @param ctx - tool registry and session working directory.
 */
export function apply(ctx) {
    ctx.tools.register(createVoxelcraftRunTool(ctx));
}
