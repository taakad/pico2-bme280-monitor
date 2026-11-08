import { connect, migrate, close } from './db.js';

const path = await migrate();
console.log(`schema applied: ${path}`);
await close();
