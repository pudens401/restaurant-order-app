// models/User.js
const mongoose = require("mongoose");

const userSchema = new mongoose.Schema({
  username: {
    type: String,
    required: true,
    unique: true,
  },
  password: {
    type: String,
    required: true, // store hashed password
  },
  role: {
    type: String,
    enum: ["admin"],
    default: "admin",
  },
}, { timestamps: true });

module.exports = mongoose.model("User", userSchema);
